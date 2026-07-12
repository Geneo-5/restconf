/**
 * @file sysrepo_plugin.c
 * @brief RESTCONF sysrepo plugin - libevent-side subscriptions.
 *
 * Architecture (ROADMAP 3.12):
 *
 * This file runs in the libevent thread and owns:
 *   - The sysrepo connection (sr_conn_ctx_t)
 *   - The subscription session (sr_session_ctx_t)
 *   - All subscriptions (oper_get, rpc_subscribe_tree,
 *     notif_subscribe) with SR_SUBSCR_NO_THREAD
 *   - The sysrepo event pipe, registered in libevent
 *
 * The worker thread (sysrepo_worker.c) handles blocking
 * GET/EDIT/RPC operations using sessions created on the
 * shared connection.
 *
 * Rationale: the oper_get callback for ietf-restconf-monitoring
 * and the rpc callback for ietf-subscribed-notifications:establish-
 * subscription provide local data (no blocking I/O).  They can
 * safely execute in the libevent thread via SR_SUBSCR_NO_THREAD +
 * sr_subscription_process_events().  This avoids sysrepo internal
 * threads and keeps all event processing in the single libevent
 * loop.
 */
#define _GNU_SOURCE
#define UNUSED __attribute__((unused))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <event2/event.h>
#include <sysrepo.h>
#include <libyang/libyang.h>
#include "plugin_api.h"
#include "sysrepo_worker.h"
#include "codec.h"
#include "logger.h"

/**
 * @brief Plugin context: owns the sysrepo connection,
 * subscription session, and all subscriptions.
 */
struct plugin_ctx_s {
	sr_conn_ctx_t *conn;
	sr_session_ctx_t *sub_session;
	sr_subscription_ctx_t *sub;
	int sr_event_fd;
	struct event *sr_event;
	struct event_base *base;
	sr_worker_t *worker;

	/* Notification callback (set by
	 * plugin_subscribe_notifications) */
	plugin_notif_cb notif_cb;
	void *notif_user_data;
};

/* ====================================================================
 * Sysrepo callbacks (executed in libevent thread context)
 * ==================================================================== */

/**
 * @brief Operational data callback for
 * ietf-restconf-monitoring:restconf-state.
 *
 * Called from sr_subscription_process_events() (libevent thread)
 * or from sr_get_data() (worker thread).  Both are safe since
 * this callback only builds static lyd_node trees and uses
 * sr_acquire_context (documented as connection-level thread-safe).
 */
static int plugin_oper_get_cb(
	sr_session_ctx_t *session,
	uint32_t sub_id UNUSED,
	const char *module_name UNUSED,
	const char *path UNUSED,
	const char *request_xpath UNUSED,
	uint32_t operation_id UNUSED,
	struct lyd_node **parent,
	void *private_data UNUSED)
{
	const struct ly_ctx *ctx = sr_acquire_context(
		sr_session_get_connection(session));
	if (!ctx) return SR_ERR_OK;

	struct lyd_node *state = NULL;
	if (lyd_new_path(NULL, ctx,
		"/ietf-restconf-monitoring:restconf-state",
		NULL, 0, &state) != LY_SUCCESS || !state) {
		sr_release_context(
			sr_session_get_connection(session));
		return SR_ERR_OK;
	}

	struct lyd_node *caps = NULL;
	if (lyd_new_inner(state, NULL,
		"capabilities", 0, &caps) == LY_SUCCESS
		&& caps) {
		lyd_new_term(caps, NULL, "capability",
			"urn:ietf:params:restconf:capability:"
			"defaults:1.0?basic-mode=report-all",
			0, NULL);
		lyd_new_term(caps, NULL, "capability",
			"urn:ietf:params:restconf:capability:"
			"with-defaults:1.0",
			0, NULL);
		lyd_new_term(caps, NULL, "capability",
			"urn:ietf:params:restconf:capability:"
			"depth:1.0",
			0, NULL);
		lyd_new_term(caps, NULL, "capability",
			"urn:ietf:params:restconf:capability:"
			"fields:1.0",
			0, NULL);
		lyd_new_term(caps, NULL, "capability",
			"urn:ietf:params:restconf:capability:"
			"with-origin:1.0",
			0, NULL);
	}

	struct lyd_node *streams = NULL;
	if (lyd_new_inner(state, NULL,
		"streams", 0, &streams) == LY_SUCCESS
		&& streams) {
		struct lyd_node *s = NULL;
		if (lyd_new_list(streams, NULL,
			"stream", 0, &s,
			"NETCONF") == LY_SUCCESS && s) {
			lyd_new_term(s, NULL,
				"description",
				"default NETCONF event stream",
				0, NULL);
			lyd_new_term(s, NULL,
				"replay-support",
				"false", 0, NULL);

			struct lyd_node *acc = NULL;
			if (lyd_new_list(s, NULL,
				"access", 0, &acc,
				"xml") == LY_SUCCESS
				&& acc) {
				lyd_new_term(acc, NULL,
					"location",
					"/restconf/data/"
					"ietf-restconf-monitoring:"
					"restconf-state/streams/"
					"stream=NETCONF/access=xml/"
					"location",
					0, NULL);
			}
			acc = NULL;
			if (lyd_new_list(s, NULL,
				"access", 0, &acc,
				"json") == LY_SUCCESS
				&& acc) {
				lyd_new_term(acc, NULL,
					"location",
					"/restconf/data/"
					"ietf-restconf-monitoring:"
					"restconf-state/streams/"
					"stream=NETCONF/access=json/"
					"location",
					0, NULL);
			}
		}
	}

	*parent = state;
	sr_release_context(
		sr_session_get_connection(session));
	return SR_ERR_OK;
}

/**
 * @brief RPC establish-subscription callback.
 *
 * Called from sr_subscription_process_events() (libevent thread)
 * or from sr_rpc_send_tree() (worker thread).
 */
static int plugin_rpc_establish_sub_cb(
	sr_session_ctx_t *session UNUSED,
	uint32_t sub_id UNUSED,
	const char *op_path UNUSED,
	const struct lyd_node *input,
	sr_event_t event,
	uint32_t request_id UNUSED,
	struct lyd_node *output,
	void *private_ctx UNUSED)
{
	if (event != SR_EV_RPC)
		return SR_ERR_OK;

	const char *stream_name = "NETCONF";
	if (input) {
		struct lyd_node *leaf = lyd_child(input);
		while (leaf) {
			if (strcmp(leaf->schema->name,
			           "stream") == 0) {
				stream_name =
					lyd_get_value(leaf);
				break;
			}
			leaf = leaf->next;
		}
	}

	static uint32_t next_sub_id = 1;
	uint32_t id = next_sub_id++;
	char id_str[32];
	snprintf(id_str, sizeof(id_str), "%u", id);
	lyd_new_term(output, NULL, "id", id_str, 0, NULL);

	(void)stream_name;
	return SR_ERR_OK;
}

/**
 * @brief Build notification envelope (RFC 8040 Sec 6.4).
 */
static char *build_notif_payload(
	const struct lyd_node *notif,
	struct timespec *timestamp)
{
	char *body = NULL;
	size_t body_len = 0;
	char time_buf[64];
	struct tm tm_utc;
	time_t sec;
	char *envelope = NULL;
	const char *inner;
	size_t inner_len;

	if (!notif) return NULL;
	if (codec_serialize_data(
		notif, MEDIA_TYPE_JSON,
		&body, &body_len) != 0 || !body)
		return NULL;

	inner = body;
	inner_len = body_len;
	if (inner_len >= 2 && inner[0] == '{' &&
	    inner[inner_len - 1] == '}') {
		inner++;
		inner_len -= 2;
	}

	sec = timestamp ? timestamp->tv_sec : time(NULL);
	gmtime_r(&sec, &tm_utc);
	strftime(time_buf, sizeof(time_buf),
	         "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

	if (asprintf(&envelope,
		"{\"ietf-restconf:notification\":{"
		"\"eventTime\":\"%s\",%.*s}}",
		time_buf,
		(int)inner_len, inner) < 0)
		envelope = NULL;

	free(body);
	return envelope;
}

/**
 * @brief Notification event callback.
 *
 * Called from sr_subscription_process_events() in the libevent
 * thread.  Since we are already in the libevent thread, we can
 * call the user callback (on_notification_cb -> sse_stream_push_event)
 * directly without marshalling through a completion queue.
 */
static void plugin_notif_event_cb(
	sr_session_ctx_t *session UNUSED,
	uint32_t sub_id UNUSED,
	const sr_ev_notif_type_t notif_type UNUSED,
	const struct lyd_node *notif,
	struct timespec *timestamp,
	void *private_data)
{
	plugin_ctx_t *ctx = (plugin_ctx_t *)private_data;
	char *payload;
	char *xpath_str;
	const char *module_name;

	if (!notif || !ctx->notif_cb) return;

	payload = build_notif_payload(notif, timestamp);
	if (!payload) return;

	module_name = (notif->schema &&
	               notif->schema->module) ?
		notif->schema->module->name : "";
	xpath_str = lyd_path(
		(struct lyd_node *)notif,
		LYD_PATH_STD, NULL, 0);

	/* Direct call - we are in the libevent thread */
	ctx->notif_cb(
		module_name,
		xpath_str ? xpath_str : "",
		payload,
		ctx->notif_user_data);

	free(xpath_str);
	free(payload);
}

/**
 * @brief libevent callback for sysrepo event pipe.
 *
 * Pumps sysrepo events (oper_get changes, rpc changes,
 * notifications) by calling sr_subscription_process_events().
 */
static void plugin_sr_event_cb(
	evutil_socket_t fd UNUSED,
	short events UNUSED, void *ctx_ptr)
{
	plugin_ctx_t *ctx = (plugin_ctx_t *)ctx_ptr;

	if (ctx->sub)
		sr_subscription_process_events(
			ctx->sub, ctx->sub_session, NULL);
}

/* ====================================================================
 * Public API
 * ==================================================================== */

plugin_ctx_t *plugin_init(
	struct event_base *base, bool use_external UNUSED,
	const char *uds_path UNUSED)
{
	plugin_ctx_t *ctx = calloc(1, sizeof(*ctx));
	if (!ctx) return NULL;

	ctx->base = base;
	ctx->sr_event_fd = -1;

	/* Create sysrepo connection */
	if (sr_connect(SR_CONN_DEFAULT, &ctx->conn) != SR_ERR_OK) {
		RC_FATAL("plugin: sr_connect failed");
		free(ctx);
		return NULL;
	}

	/* Create subscription session */
	if (sr_session_start(ctx->conn, SR_DS_RUNNING,
		&ctx->sub_session) != SR_ERR_OK) {
		RC_FATAL("plugin: sr_session_start failed");
		sr_disconnect(ctx->conn);
		free(ctx);
		return NULL;
	}

	/* Subscribe to operational data for
	 * ietf-restconf-monitoring (SR_SUBSCR_NO_THREAD) */
	int rc = sr_oper_get_subscribe(
		ctx->sub_session,
		"ietf-restconf-monitoring",
		"/ietf-restconf-monitoring:restconf-state",
		plugin_oper_get_cb, ctx,
		SR_SUBSCR_NO_THREAD | SR_SUBSCR_ENABLED,
		&ctx->sub);
	if (rc != SR_ERR_OK) {
		RC_WARN("plugin: oper_get subscribe failed: %s",
			sr_strerror(rc));
	}

	/* Subscribe to RPC establish-subscription
	 * (SR_SUBSCR_NO_THREAD) */
	rc = sr_rpc_subscribe_tree(
		ctx->sub_session,
		"/ietf-subscribed-notifications:"
		"establish-subscription",
		plugin_rpc_establish_sub_cb, ctx, 0,
		SR_SUBSCR_NO_THREAD | SR_SUBSCR_ENABLED,
		&ctx->sub);
	if (rc != SR_ERR_OK) {
		RC_WARN("plugin: rpc subscribe failed: %s",
			sr_strerror(rc));
	}

	/* Get event pipe and register in libevent */
	if (ctx->sub) {
		int pipe_fd = -1;
		if (sr_get_event_pipe(ctx->sub, &pipe_fd)
		    == SR_ERR_OK) {
			ctx->sr_event_fd = pipe_fd;
			ctx->sr_event = event_new(
				base, pipe_fd,
				EV_READ | EV_PERSIST,
				plugin_sr_event_cb, ctx);
			if (ctx->sr_event)
				event_add(ctx->sr_event, NULL);
		}
	}

	/* Create worker thread (passes conn for GET/EDIT/RPC) */
	ctx->worker = sr_worker_create(base, ctx->conn);
	if (!ctx->worker) {
		RC_FATAL("plugin: worker creation failed");
		if (ctx->sr_event) event_free(ctx->sr_event);
		if (ctx->sub) sr_unsubscribe(ctx->sub);
		sr_session_stop(ctx->sub_session);
		sr_disconnect(ctx->conn);
		free(ctx);
		return NULL;
	}

	RC_INFO("plugin initialized (libevent subscriptions + "
		"worker mode)");
	return ctx;
}

void plugin_handle_get(
	plugin_ctx_t *ctx, const rc_request_t *req,
	plugin_data_cb callback, void *user_data)
{
	if (!ctx || !ctx->worker) {
		callback(500, NULL, 0, NULL, user_data);
		return;
	}

	sr_worker_submit_get(
		ctx->worker,
		req->res_type,
		req->datastore,
		req->username,
		req->xpath,
		req->content_filter,
		req->depth,
		req->fields_expr,
		req->with_defaults,
		req->with_origin,
		req->accept_type,
		callback,
		user_data);
}

void plugin_handle_rpc(
	plugin_ctx_t *ctx,
	const rc_request_t *req,
	const uint8_t *body,
	size_t body_len,
	plugin_rpc_cb callback,
	void *user_data)
{
	if (!ctx || !ctx->worker) {
		callback(500, NULL, 0, user_data);
		return;
	}

	sr_worker_submit_rpc(
		ctx->worker,
		req->username,
		req->rpc_module,
		req->rpc_name,
		req->req_type,
		req->accept_type,
		body, body_len,
		callback,
		user_data);
}

void plugin_handle_edit(
	plugin_ctx_t *ctx,
	const rc_request_t *req,
	const uint8_t *body,
	size_t body_len,
	plugin_edit_cb callback,
	void *user_data)
{
	if (!ctx || !ctx->worker) {
		callback(500, "operation-failed",
		         "Plugin not initialized",
		         NULL, user_data);
		return;
	}

	sr_worker_submit_edit(
		ctx->worker,
		req->datastore,
		req->username,
		req->xpath,
		req->method,
		req->req_type,
		req->accept_type,
		req->if_match,
		body, body_len,
		callback,
		user_data);
}

void plugin_subscribe_notifications(
	plugin_ctx_t *ctx, plugin_notif_cb callback,
	void *user_data)
{
	if (!ctx) return;

	ctx->notif_cb = callback;
	ctx->notif_user_data = user_data;

	/* Subscribe to notifications with SR_SUBSCR_NO_THREAD.
	 * The callback will be invoked from
	 * sr_subscription_process_events() in the libevent thread.
	 * Use sr_notif_subscribe_tree for tree-based callbacks
	 * (struct lyd_node instead of sr_val_t). */
	if (ctx->sub_session) {
		int rc = sr_notif_subscribe_tree(
			ctx->sub_session,
			"restconf-test",
			NULL, NULL, NULL,
			plugin_notif_event_cb,
			ctx,
			SR_SUBSCR_NO_THREAD,
			&ctx->sub);
		if (rc != SR_ERR_OK) {
			RC_WARN("plugin: notif subscribe failed: %s",
				sr_strerror(rc));
		}

		/* Re-read event pipe after new subscription
		 * (pipe may have changed) */
		if (ctx->sub) {
			int pipe_fd = -1;
			if (sr_get_event_pipe(ctx->sub, &pipe_fd)
			    == SR_ERR_OK &&
			    pipe_fd != ctx->sr_event_fd) {
				/* Update the libevent registration */
				if (ctx->sr_event)
					event_free(ctx->sr_event);
				ctx->sr_event_fd = pipe_fd;
				ctx->sr_event = event_new(
					ctx->base, pipe_fd,
					EV_READ | EV_PERSIST,
					plugin_sr_event_cb, ctx);
				if (ctx->sr_event)
					event_add(ctx->sr_event, NULL);
			}
		}
	}
}

void plugin_destroy(plugin_ctx_t *ctx)
{
	if (!ctx) return;

	/* Destroy worker first (stops the thread) */
	sr_worker_destroy(ctx->worker);

	/* Cleanup libevent event */
	if (ctx->sr_event)
		event_free(ctx->sr_event);

	/* Cleanup sysrepo subscriptions */
	if (ctx->sub)
		sr_unsubscribe(ctx->sub);

	/* Cleanup session and connection */
	if (ctx->sub_session)
		sr_session_stop(ctx->sub_session);
	sr_disconnect(ctx->conn);

	free(ctx);
}

/**
 * @brief Acquire libyang context (delegates to worker).
 *
 * sr_acquire_context is documented by sysrepo as
 * connection-level thread-safe, so this is the only
 * sysrepo/libyang call allowed from the libevent thread
 * (cf. AGENTS.md rule #1 exception, ROADMAP 3.12).
 */
const struct ly_ctx *plugin_acquire_ly_ctx(plugin_ctx_t *ctx)
{
	if (!ctx || !ctx->worker) return NULL;
	return sr_worker_acquire_ly_ctx(ctx->worker);
}

void plugin_release_ly_ctx(plugin_ctx_t *ctx)
{
	if (ctx && ctx->worker)
		sr_worker_release_ly_ctx(ctx->worker);
}
