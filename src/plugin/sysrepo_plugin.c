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

/**
 * @brief Callback libevent déclenché quand le pipe sysrepo est lisible.
 * Appelle sr_subscription_process_events pour drainer le pipe et
 * exécuter les callbacks sysrepo (RPC, oper, notif) dans le thread
 * unique de libevent, sans jamais bloquer.
 */
static void sr_event_cb(
	evutil_socket_t fd UNUSED,
	short events UNUSED, void *ctx_ptr)
{
	plugin_ctx_t *plugin = (plugin_ctx_t *)ctx_ptr;
	if (plugin->subscription) {
		sr_subscription_process_events(
			plugin->subscription, NULL, NULL);
	}
}

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
	/* TODO: Générer les données opérationnelles */
	return SR_ERR_OK;
}

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
	/* TODO: Créer la souscription et retourner l'URI SSE */
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

	/* Abonnement aux données opérationnelles.
	 * SR_SUBSCR_NO_THREAD est CRUCIAL pour éviter que sysrepo ne crée
	 * son propre thread, ce qui violerait la contrainte mono-thread. */
	sr_oper_get_subscribe(
		ctx->session, "ietf-restconf-monitoring",
		"/ietf-restconf-monitoring:restconf-state",
		oper_get_cb, NULL, SR_SUBSCR_NO_THREAD,
		&ctx->subscription);

	/* Abonnement au RPC establish-subscription */
	sr_rpc_subscribe_tree(
		ctx->session,
		"/ietf-subscribed-notifications:establish-subscription",
		rpc_establish_sub_cb, NULL, 0,
		SR_SUBSCR_NO_THREAD, &ctx->subscription);

	/* Intégration du pipe d'événement sysrepo dans libevent.
	 * sr_get_event_pipe ne retourne qu'un seul FD (le read pipe). */
	int event_pipe = -1;
	int rc = sr_get_event_pipe(
		ctx->subscription, &event_pipe);
	
	if (rc == SR_ERR_OK && event_pipe >= 0) {
		ctx->sr_event = event_new(
			ctx->base, event_pipe,
			EV_READ | EV_PERSIST, sr_event_cb, ctx);
		if (ctx->sr_event) {
			event_add(ctx->sr_event, NULL);
		}
	}

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
		/* Retirer l'événement libevent en premier */
		if (ctx->sr_event) {
			event_free(ctx->sr_event);
		}
		/* Désabonner sysrepo (ferme le pipe interne) */
		if (ctx->subscription) {
			sr_unsubscribe(ctx->subscription);
		}
		if (ctx->session) sr_session_stop(ctx->session);
		if (ctx->conn) sr_disconnect(ctx->conn);
		free(ctx);
	}
}

const struct ly_ctx *plugin_acquire_ly_ctx(plugin_ctx_t *ctx) {
	if (!ctx || !ctx->conn) return NULL;
	return sr_acquire_context(ctx->conn);
}

void plugin_release_ly_ctx(plugin_ctx_t *ctx) {
	if (ctx && ctx->conn) {
		sr_release_context(ctx->conn);
	}
}