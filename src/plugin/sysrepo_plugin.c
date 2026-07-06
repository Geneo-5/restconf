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
	sr_subscription_ctx_t *subscription; /* Ajouté pour gérer les abonnements */
	struct event_base *base;
	struct event *sr_event;
	plugin_notif_cb notif_cb;
	void *notif_user_data;
};

static void sr_event_cb(
	evutil_socket_t fd UNUSED,
	short events UNUSED, void *ctx)
{
	plugin_ctx_t *plugin = (plugin_ctx_t *)ctx;
	/* Traitement asynchrone des événements sysrepo dans la boucle libevent */
	if (plugin->subscription) {
		sr_subscription_process_events(
			plugin->subscription, NULL, NULL);
	}
}

/* Callback pour les données opérationnelles (ietf-restconf-monitoring) */
/* Signature conforme à sr_oper_get_items_cb */
static int oper_get_cb(
	sr_session_ctx_t *session UNUSED,
	uint32_t sub_id UNUSED,
	const char *module_name UNUSED,
	const char *path UNUSED,
	const char *request_xpath UNUSED,
	uint32_t operation_id UNUSED,
	struct lyd_node **parent,
	void *private_data UNUSED)
{
	/* TODO: Générer les données opérationnelles et les ajouter à *parent */
	/* Exemple : lyd_new_term(*parent, NULL, "capability", "urn:...", NULL); */
	return SR_ERR_OK;
}

/* Callback pour les notifications YANG */
/* Signature conforme à sr_event_notif_cb */
static void notif_cb_sr(
	sr_session_ctx_t *session UNUSED,
	uint32_t sub_id UNUSED,
	sr_ev_notif_type_t notif_type UNUSED, /* Correction du type */
	const char *xpath UNUSED,
	const sr_val_t *values UNUSED,
	const size_t values_cnt UNUSED,
	struct timespec *timestamp UNUSED,
	void *private_data UNUSED)
{
	/* TODO: Formater et pousser la notification via le plugin_ctx */
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

	/* Abonnement aux données opérationnelles */
	/* Signature conforme à sr_oper_get_subscribe */
	sr_oper_get_subscribe(
		ctx->session, "ietf-restconf-monitoring",
		"/ietf-restconf-monitoring:restconf-state",
		oper_get_cb, NULL, 0, &ctx->subscription);

	/* TODO: Intégrer le FD de subscription dans libevent si nécessaire */

	return ctx;
}

void plugin_handle_get(
	plugin_ctx_t *ctx, const rc_request_t *req,
	plugin_data_cb callback, void *user_data)
{
	sr_data_t *data = NULL;
	
	/* Application du contexte utilisateur pour le NACM */
	if (req->username) {
		sr_session_set_user(ctx->session, req->username);
	}

	/* Récupération des données */
	int rc = sr_get_data(
		ctx->session, req->xpath, 0, 0, 0, &data);
	
	/* Appel du callback pour envoyer la réponse HTTP/2 */
	callback(data, rc, user_data);

	if (data) sr_release_data(data);
}

void plugin_handle_edit(
	plugin_ctx_t *ctx UNUSED,
	const rc_request_t *req UNUSED,
	plugin_status_cb callback UNUSED,
	void *user_data UNUSED)
{
	/* TODO: Implémenter les opérations d'édition (POST/PUT/PATCH/DELETE) */
}

void plugin_subscribe_notifications(
	plugin_ctx_t *ctx, plugin_notif_cb callback,
	void *user_data)
{
	ctx->notif_cb = callback;
	ctx->notif_user_data = user_data;
	
	/* L'abonnement aux notifications peut être ajouté ici via 
	   sr_event_notif_subscribe quand la logique sera implémentée */
}

void plugin_destroy(plugin_ctx_t *ctx) {
	if (ctx) {
		/* Désabonnement propre */
		if (ctx->subscription) {
			sr_unsubscribe(ctx->subscription);
		}
		if (ctx->sr_event) event_free(ctx->sr_event);
		if (ctx->session) sr_session_stop(ctx->session);
		if (ctx->conn) sr_disconnect(ctx->conn);
		free(ctx);
	}
}