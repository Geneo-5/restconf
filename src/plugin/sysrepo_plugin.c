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
	sr_session_ctx_t *sess_running;
	sr_session_ctx_t *sess_operational;
	sr_session_ctx_t *sess_startup;
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

	/* Créer des sessions pour chaque datastore */
	sr_session_start(
		ctx->conn, SR_DS_RUNNING, &ctx->sess_running);
	sr_session_start(
		ctx->conn, SR_DS_OPERATIONAL, &ctx->sess_operational);
	sr_session_start(
		ctx->conn, SR_DS_STARTUP, &ctx->sess_startup);

	/* Session par défaut (pour compatibilité) */
	ctx->session = ctx->sess_running;

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

/**
 * @brief Sélectionne la session sysrepo correspondant au datastore.
 */
static sr_session_ctx_t *select_session(
	plugin_ctx_t *ctx, rc_datastore_t ds)
{
	switch (ds) {
	case RC_DS_RUNNING:
		return ctx->sess_running;
	case RC_DS_OPERATIONAL:
		return ctx->sess_operational;
	case RC_DS_INTENDED:
		/* INTENDED n'a pas de session sysrepo directe,
		 * c'est un datastore conceptuel NMDA.
		 * Pour l'instant, fallback sur operational. */
		return ctx->sess_operational;
	default:
		return ctx->sess_running;
	}
}

void plugin_handle_get(
	plugin_ctx_t *ctx, const rc_request_t *req,
	plugin_data_cb callback, void *user_data)
{
	sr_data_t *data = NULL;
	sr_session_ctx_t *sess = select_session(
		ctx, req->datastore);
	
	if (req->username) {
		sr_session_set_user(sess, req->username);
	}

	/* Note: sr_get_data() utilise la mémoire partagée (SHM)
	 * de sysrepo, ce qui rend l'opération très rapide.
	 * Le "blocage" est minime (accès mémoire, pas réseau). */
	int rc = sr_get_data(sess, req->xpath, 0, 0, 0, &data);
	
	callback(data, rc, user_data);

	if (data) sr_release_data(data);
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
		if (ctx->sess_running) {
			sr_session_stop(ctx->sess_running);
		}
		if (ctx->sess_operational) {
			sr_session_stop(ctx->sess_operational);
		}
		if (ctx->sess_startup) {
			sr_session_stop(ctx->sess_startup);
		}
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

void plugin_handle_rpc(
	plugin_ctx_t *ctx UNUSED,
	const rc_request_t *req UNUSED,
	plugin_rpc_cb callback,
	void *user_data)
{
	/* TODO: Implémenter l'invocation RPC via sysrepo */
	/* Pour l'instant, retourner une erreur */
	callback(NULL, SR_ERR_OPERATION_FAILED, user_data);
}

void plugin_handle_edit(
	plugin_ctx_t *ctx,
	const rc_request_t *req,
	const uint8_t *body,
	size_t body_len,
	plugin_edit_cb callback,
	void *user_data)
{
	int rc = SR_ERR_OK;
	int http_status = 204;
	const char *error_tag = "operation-failed";
	const char *error_msg = "Success";

	sr_session_ctx_t *sess = select_session(
		ctx, req->datastore);

	/* RFC 8527 Sec 3.2: operational est en lecture seule */
	if (req->datastore == RC_DS_OPERATIONAL) {
		callback(405, "operation-not-supported",
		         "Cannot edit operational datastore",
		         user_data);
		return;
	}

	if (req->username) {
		sr_session_set_user(sess, req->username);
	}

	if (strcmp(req->method, "DELETE") == 0) {
		rc = sr_delete_item(
			sess, req->xpath, SR_EDIT_DEFAULT);
		if (rc == SR_ERR_OK) {
			/* Ajout du timeout (0 = défaut) */
			rc = sr_apply_changes(sess, 0);
		}
	} else {
		/* POST, PUT, PATCH */
		struct lyd_node *data = NULL;
		rc = codec_parse_data(
			sess, (const char *)body,
			body_len, req->req_type, &data);
		
		if (rc == 0 && data) {
			const char *default_op = "merge";
			
			if (strcmp(req->method, "PUT") == 0) {
				default_op = "replace";
			} else if (strcmp(req->method, "POST") == 0) {
				default_op = "merge";
				http_status = 201; /* Created */
			} else if (strcmp(req->method, "PATCH") == 0) {
				default_op = "merge";
			}

			rc = sr_edit_batch(sess, data, default_op);
			if (rc == SR_ERR_OK) {
				/* Ajout du timeout (0 = défaut) */
				rc = sr_apply_changes(sess, 0);
			}
			lyd_free_all(data);
		} else {
			rc = SR_ERR_VALIDATION_FAILED;
		}
	}

	/* Mapping des erreurs sysrepo vers HTTP/RESTCONF */
	error_msg = sr_strerror(rc);
	if (rc != SR_ERR_OK) {
		if (rc == SR_ERR_UNAUTHORIZED) {
			error_tag = "access-denied";
			http_status = 403;
		} else if (rc == SR_ERR_NOT_FOUND) {
			error_tag = "invalid-value";
			http_status = 404;
		} else if (rc == SR_ERR_EXISTS) {
			error_tag = "data-exists";
			http_status = 409;
		} else if (rc == SR_ERR_VALIDATION_FAILED) {
			error_tag = "invalid-value";
			http_status = 400;
		} else {
			http_status = 500;
		}
	}

	callback(http_status, error_tag, error_msg, user_data);
}