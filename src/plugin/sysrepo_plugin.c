#define _GNU_SOURCE
#define UNUSED __attribute__((unused))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysrepo.h>
#include <libyang/libyang.h>
#include <event2/event.h>
#include "plugin_api.h"

struct plugin_ctx_s {
	sr_conn_ctx_t *conn;
	sr_session_ctx_t *session;
	sr_subscription_ctx_t *subscription;
	struct event_base *base;
	struct event *sr_event;
	plugin_notif_cb notif_cb;
	void *notif_user_data;
};

/* Callback pour les données opérationnelles */
static int oper_get_cb(
	sr_session_ctx_t *session UNUSED,
	uint32_t sub_id UNUSED,
	const char *module_name UNUSED,
	const char *path UNUSED,
	const char *request_xpath UNUSED,
	uint32_t operation_id UNUSED,
	struct lyd_node **parent UNUSED,
	void *private_data UNUSED)
{
	return SR_ERR_OK;
}

/* Callback pour le RPC establish-subscription */
static int rpc_establish_sub_cb(
	sr_session_ctx_t *session UNUSED,
	uint32_t sub_id UNUSED,
	const char *op_path UNUSED,
	const struct lyd_node *input UNUSED,
	sr_event_t event UNUSED,
	uint32_t request_id UNUSED,
	struct lyd_node *output UNUSED,
	void *private_ctx UNUSED)
{
	return SR_ERR_OK;
}

plugin_ctx_t *plugin_init(
	struct event_base *base, bool use_external UNUSED,
	const char *uds_path UNUSED)
{
	plugin_ctx_t *ctx = calloc(1, sizeof(plugin_ctx_t));
	ctx->base = base;

	if (sr_connect(SR_CONN_DEFAULT, &ctx->conn) != SR_ERR_OK) {
		free(ctx); return NULL;
	}
	sr_session_start(ctx->conn, SR_DS_OPERATIONAL, &ctx->session);

	sr_oper_get_subscribe(
		ctx->session, "ietf-restconf-monitoring",
		"/ietf-restconf-monitoring:restconf-state",
		oper_get_cb, NULL, 0, &ctx->subscription);

	sr_rpc_subscribe_tree(
		ctx->session,
		"/ietf-subscribed-notifications:establish-subscription",
		rpc_establish_sub_cb, NULL, 0, 0, NULL);

	return ctx;
}

void plugin_handle_get(
	plugin_ctx_t *ctx, const rc_request_t *req,
	plugin_data_cb callback, void *user_data)
{
	sr_data_t *data = NULL;
	
	if (req->username) {
		sr_session_set_user(ctx->session, req->username);
	}

	int rc = sr_get_data(
		ctx->session, req->xpath, 0, 0, 0, &data);
	
	callback(data, rc, user_data);

	if (data) sr_release_data(data);
}

void plugin_handle_edit(
	plugin_ctx_t *ctx UNUSED,
	const rc_request_t *req UNUSED,
	plugin_status_cb callback UNUSED,
	void *user_data UNUSED)
{
	/* TODO: Implémenter les opérations d'édition */
}

void plugin_subscribe_notifications(
	plugin_ctx_t *ctx, plugin_notif_cb callback,
	void *user_data)
{
	ctx->notif_cb = callback;
	ctx->notif_user_data = user_data;
}

void plugin_destroy(plugin_ctx_t *ctx) {
	if (ctx) {
		if (ctx->subscription) {
			sr_unsubscribe(ctx->subscription);
		}
		if (ctx->sr_event) event_free(ctx->sr_event);
		if (ctx->session) sr_session_stop(ctx->session);
		if (ctx->conn) sr_disconnect(ctx->conn);
		free(ctx);
	}
}