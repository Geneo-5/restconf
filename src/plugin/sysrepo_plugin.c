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

/**
 * @brief Callback pour les données opérationnelles ietf-restconf-monitoring.
 * Génère dynamiquement les capacités et streams supportés.
 * RFC 8040 Sec 9 et RFC 8527 Sec 4.
 */
static int oper_get_cb(
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

	/* Créer le conteneur restconf-state */
	struct lyd_node *state = NULL;
	if (lyd_new_path(
		NULL, ctx,
		"/ietf-restconf-monitoring:restconf-state",
		NULL, 0, &state) != LY_SUCCESS || !state) {
		sr_release_context(sr_session_get_connection(session));
		return SR_ERR_OK;
	}

	/* Conteneur capabilities */
	struct lyd_node *caps = NULL;
	if (lyd_new_inner(
		state, NULL, "capabilities", 0, &caps) == LY_SUCCESS
		&& caps) {
		/* defaults capability (RFC 8040 Sec 9.1.2) */
		lyd_new_term(caps, NULL, "capability",
			"urn:ietf:params:restconf:capability:"
			"defaults:1.0?basic-mode=report-all",
			0, NULL);
		/* with-defaults (RFC 8040 Sec 4.8.9) */
		lyd_new_term(caps, NULL, "capability",
			"urn:ietf:params:restconf:capability:"
			"with-defaults:1.0",
			0, NULL);
		/* depth (RFC 8040 Sec 4.8.2) */
		lyd_new_term(caps, NULL, "capability",
			"urn:ietf:params:restconf:capability:"
			"depth:1.0",
			0, NULL);
		/* fields (RFC 8040 Sec 4.8.3) */
		lyd_new_term(caps, NULL, "capability",
			"urn:ietf:params:restconf:capability:"
			"fields:1.0",
			0, NULL);
		/* with-origin (RFC 8527 Sec 3.2.2) */
		lyd_new_term(caps, NULL, "capability",
			"urn:ietf:params:restconf:capability:"
			"with-origin:1.0",
			0, NULL);
	}

	/* Conteneur streams */
	struct lyd_node *streams = NULL;
	if (lyd_new_inner(
		state, NULL, "streams", 0, &streams) == LY_SUCCESS
		&& streams) {
		/* Stream NETCONF par défaut (RFC 8040 Sec 6.3.1) */
		struct lyd_node *s = NULL;
		if (lyd_new_list(
			streams, NULL, "stream", 0, &s,
			"NETCONF") == LY_SUCCESS && s) {
			lyd_new_term(s, NULL, "description",
				"default NETCONF event stream",
				0, NULL);
			lyd_new_term(s, NULL, "replay-support",
				"false", 0, NULL);

			/* Accès XML */
			struct lyd_node *acc = NULL;
			if (lyd_new_list(
				s, NULL, "access", 0, &acc,
				"xml") == LY_SUCCESS && acc) {
				lyd_new_term(acc, NULL,
					"location",
					"/restconf/data/"
					"ietf-restconf-monitoring:"
					"restconf-state/streams/"
					"stream=NETCONF/access=xml/"
					"location",
					0, NULL);
			}

			/* Accès JSON */
			acc = NULL;
			if (lyd_new_list(
				s, NULL, "access", 0, &acc,
				"json") == LY_SUCCESS && acc) {
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
	sr_release_context(sr_session_get_connection(session));
	return SR_ERR_OK;
}

/**
 * @brief Callback pour le RPC establish-subscription.
 * Crée un abonnement sysrepo aux notifications et retourne l'URI SSE.
 * YANG ietf-subscribed-notifications.
 */
static int rpc_establish_sub_cb(
	sr_session_ctx_t *session UNUSED,
	uint32_t sub_id UNUSED,
	const char *op_path UNUSED,
	const struct lyd_node *input,
	sr_event_t event,
	uint32_t request_id UNUSED,
	struct lyd_node *output,
	void *private_ctx)
{
	plugin_ctx_t *plugin = (plugin_ctx_t *)private_ctx;

	if (event != SR_EV_RPC) {
		return SR_ERR_OK;
	}

	/* Extraire le stream demandé depuis l'input */
	const char *stream_name = "NETCONF";
	if (input) {
		struct lyd_node *stream_leaf = lyd_child(input);
		while (stream_leaf) {
			if (strcmp(stream_leaf->schema->name,
			           "stream") == 0) {
				stream_name = lyd_get_value(stream_leaf);
				break;
			}
			stream_leaf = stream_leaf->next;
		}
	}

	/* Générer un ID de souscription unique */
	static uint32_t next_sub_id = 1;
	uint32_t id = next_sub_id++;

	/* Ajouter l'ID de souscription dans l'output */
	char id_str[32];
	snprintf(id_str, sizeof(id_str), "%u", id);
	lyd_new_term(output, NULL, "id", id_str, 0, NULL);

	/* TODO: Abonner sysrepo aux notifications du stream demandé
	 * et câbler le callback vers sse_stream_push_event.
	 * Pour l'instant, on se contente de retourner l'ID. */
	(void)stream_name;

	(void)plugin;
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
	 * son propre thread, ce qui violerait la contrainte mono-thread.
	 * On passe ctx comme private_data pour les callbacks. */
	sr_oper_get_subscribe(
		ctx->session, "ietf-restconf-monitoring",
		"/ietf-restconf-monitoring:restconf-state",
		oper_get_cb, ctx, SR_SUBSCR_NO_THREAD,
		&ctx->subscription);

	/* Abonnement au RPC establish-subscription */
	sr_rpc_subscribe_tree(
		ctx->session,
		"/ietf-subscribed-notifications:establish-subscription",
		rpc_establish_sub_cb, ctx, 0,
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

/*
 * Construit la reponse RESTCONF finale (statut HTTP + corps
 * serialise JSON/XML) a partir du resultat brut de sr_get_data().
 * Applique le filtre "fields" (RFC 8040 Sec 4.8.3) et
 * "with-defaults" (RFC 8040 Sec 4.8.9). Mutualisee entre le mode
 * interne et le mode externe : le daemon restconf-plugin (mode
 * externe) appelle plugin_handle_get() telle quelle, ce qui evite
 * de dupliquer cette logique cote gateway (voir uds_plugin.c).
 */
static void build_get_response(
	const rc_request_t *req, sr_data_t *data, int rc,
	int *out_status, uint8_t **out_body, size_t *out_len)
{
	char *body = NULL;
	size_t body_len = 0;
	int status = 200;

	if (rc != SR_ERR_OK) {
		const char *tag = "operation-failed";
		const char *msg = sr_strerror(rc);

		/* RFC 8527 Sec 3.1/3.2: identityref de datastore
		 * inconnue ou non supportee (dynamique). */
		if (rc == SR_ERR_INVAL_ARG) {
			status = 400;
			tag = "invalid-value";
			msg = "Unknown or unsupported datastore";
		} else if (rc == SR_ERR_NOT_FOUND) {
			status = 404;
			tag = "invalid-value";
		} else if (rc == SR_ERR_UNAUTHORIZED) {
			status = 403;
			tag = "access-denied";
		} else {
			status = 500;
		}
		codec_serialize_errors(
			req->accept_type, tag, msg,
			&body, &body_len);
	} else if (data && data->tree) {
		struct lyd_node *filtered = NULL;

		if (req->fields_expr &&
		    codec_filter_fields(
				data->tree, req->fields_expr,
				&filtered) == 0 && filtered) {
			if (codec_serialize_data_wd(
					filtered, req->accept_type,
					req->with_defaults,
					&body, &body_len) != 0) {
				status = 500;
				codec_serialize_errors(
					req->accept_type,
					"operation-failed",
					"Serialization failed",
					&body, &body_len);
			}
			lyd_free_all(filtered);
		} else if (codec_serialize_data_wd(
				data->tree, req->accept_type,
				req->with_defaults,
				&body, &body_len) != 0) {
			status = 500;
			codec_serialize_errors(
				req->accept_type,
				"operation-failed",
				"Serialization failed",
				&body, &body_len);
		}
	} else {
		status = 204;
	}

	*out_status = status;
	*out_body = (uint8_t *)body;
	*out_len = body_len;
}

void plugin_handle_get(
	plugin_ctx_t *ctx, const rc_request_t *req,
	plugin_data_cb callback, void *user_data)
{
	/* RFC 8527 Sec 3.1: seuls running/operational/intended sont
	 * des datastores NMDA reconnus ; toute autre identityref
	 * (datastore dynamique ou inconnu) n'est pas supportée. */
	if (req->datastore == RC_DS_UNKNOWN) {
		int status;
		uint8_t *body;
		size_t body_len;

		build_get_response(
			req, NULL, SR_ERR_INVAL_ARG,
			&status, &body, &body_len);
		callback(status, body, body_len, user_data);
		/* NOTE: body est libéré par le callback (get_data_cb) */
		return;
	}

	sr_data_t *data = NULL;
	sr_session_ctx_t *sess = select_session(
		ctx, req->datastore);
	
	if (req->username) {
		sr_session_set_user(sess, req->username);
	}

	/* RFC 8040 Sec 4.8.1 : "content" restreint les données de
	 * configuration et/ou d'état renvoyées par le serveur. */
	sr_get_oper_flag_t opts = 0;
	if (req->content_filter) {
		if (strcmp(req->content_filter, "config") == 0) {
			opts |= SR_OPER_NO_STATE;
		} else if (strcmp(
				req->content_filter, "nonconfig") == 0) {
			opts |= SR_OPER_NO_CONFIG;
		}
		/* "all" (valeur par défaut) : aucun filtre */
	}

	/* RFC 8527 Sec 3.2.2 : "with-origin" annote les données
	 * opérationnelles avec leur source NMDA (intended,
	 * default, learned, system, unknown). */
	if (req->with_origin &&
	    req->datastore == RC_DS_OPERATIONAL) {
		opts |= SR_OPER_WITH_ORIGIN;
	}

	/* RFC 8040 Sec 4.8.2 : "depth" limite la profondeur des
	 * sous-arbres retournés. depth == -1 (absent ou
	 * "unbounded") correspond à 0 côté sysrepo (illimité). */
	uint32_t max_depth = (req->depth > 0) ?
		(uint32_t)req->depth : 0;

	int status;
	uint8_t *body;
	size_t body_len;

	/* Validation du xpath : vérifier que le premier module
	 * référencé existe dans le contexte libyang courant.
	 * Cela évite un crash potentiel de sysrepo sur un xpath
	 * pointant vers un module non chargé. */
	if (req->xpath) {
		const struct ly_ctx *ly_ctx = sr_acquire_context(
			ctx->conn);
		if (ly_ctx) {
			const char *p = req->xpath;
			if (*p == '/') p++;
			const char *colon = strchr(p, ':');
			if (colon) {
				char mod_name[256];
				size_t mod_len = (size_t)(colon - p);
				if (mod_len > 0 &&
				    mod_len < sizeof(mod_name)) {
					memcpy(mod_name, p, mod_len);
					mod_name[mod_len] = '\0';
					if (!ly_ctx_get_module_implemented(
					ly_ctx, mod_name)) {
					sr_release_context(ctx->conn);
					/* Module inexistant → 404 */
					build_get_response(
					req, NULL,
					SR_ERR_NOT_FOUND,
					&status, &body,
					&body_len);
					callback(status, body,
					body_len,
					user_data);
					/* NOTE: body est libéré par le callback */
					return;
					}
				}
			}
			sr_release_context(ctx->conn);
		}
	}

	/* Note: sr_get_data() utilise la mémoire partagée (SHM)
	 * de sysrepo, ce qui rend l'opération très rapide.
	 * Le "blocage" est minime (accès mémoire, pas réseau). */
	int rc = sr_get_data(
		sess, req->xpath, max_depth, 0, opts, &data);

	build_get_response(
		req, data, rc, &status, &body, &body_len);
	callback(status, body, body_len, user_data);
	/* NOTE: body est libéré par le callback (get_data_cb) */

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

/*
 * Construit la reponse RESTCONF finale (statut HTTP + corps
 * serialise) pour l'output d'un RPC/action (RFC 8040 Sec 3.6.2).
 * Mutualisee entre le mode interne et le mode externe, au meme
 * titre que build_get_response() ci-dessus.
 */
static void build_rpc_response(
	const rc_request_t *req, sr_data_t *output, int rc,
	int *out_status, uint8_t **out_body, size_t *out_len)
{
	char *body = NULL;
	size_t body_len = 0;
	int status = 200;

	if (rc != SR_ERR_OK) {
		const char *tag = "operation-failed";
		const char *msg = sr_strerror(rc);

		if (rc == SR_ERR_UNAUTHORIZED) {
			status = 403;
			tag = "access-denied";
		} else if (rc == SR_ERR_NOT_FOUND) {
			status = 404;
			tag = "invalid-value";
		} else if (rc == SR_ERR_INVAL_ARG) {
			status = 400;
			tag = "invalid-value";
		} else if (rc == SR_ERR_VALIDATION_FAILED) {
			status = 400;
			tag = "invalid-value";
		} else {
			status = 500;
		}
		codec_serialize_errors(
			req->accept_type, tag, msg,
			&body, &body_len);
	} else if (output && output->tree) {
		if (codec_serialize_data(
				output->tree, req->accept_type,
				&body, &body_len) != 0) {
			status = 500;
			codec_serialize_errors(
				req->accept_type,
				"operation-failed",
				"RPC output serialization failed",
				&body, &body_len);
		}
	} else {
		status = 204;
	}

	*out_status = status;
	*out_body = (uint8_t *)body;
	*out_len = body_len;
}

void plugin_handle_rpc(
	plugin_ctx_t *ctx,
	const rc_request_t *req,
	const uint8_t *body,
	size_t body_len,
	plugin_rpc_cb callback,
	void *user_data)
{
	int rc = SR_ERR_OK;
	struct lyd_node *input = NULL;
	sr_data_t *output = NULL;
	int out_status;
	uint8_t *out_body;
	size_t out_len;

	if (!req->rpc_module || !req->rpc_name) {
		build_rpc_response(
			req, NULL, SR_ERR_INVAL_ARG,
			&out_status, &out_body, &out_len);
		callback(out_status, out_body, out_len, user_data);
		/* NOTE: out_body est libéré par le callback */
		return;
	}

	/* Parser le body d'entrée (input du RPC) */
	if (body && body_len > 0) {
		rc = codec_parse_data(
			ctx->sess_running,
			(const char *)body, body_len,
			req->req_type, &input);
		if (rc != 0) {
			build_rpc_response(
				req, NULL, SR_ERR_VALIDATION_FAILED,
				&out_status, &out_body, &out_len);
			callback(out_status, out_body,
			         out_len, user_data);
			/* NOTE: out_body est libéré par le callback */
			return;
		}
	} else {
		/* RPC sans input : créer le nœud racine RPC */
		const struct ly_ctx *ly_ctx =
			sr_acquire_context(ctx->conn);
		if (!ly_ctx) {
			build_rpc_response(
				req, NULL, SR_ERR_OPERATION_FAILED,
				&out_status, &out_body, &out_len);
			callback(out_status, out_body,
			         out_len, user_data);
			/* NOTE: out_body est libéré par le callback */
			return;
		}
		char rpc_path[512];
		snprintf(rpc_path, sizeof(rpc_path),
		         "/%s:%s",
		         req->rpc_module, req->rpc_name);
		rc = lyd_new_path(NULL, ly_ctx, rpc_path,
		                  NULL, 0, &input);
		sr_release_context(ctx->conn);
		if (rc != LY_SUCCESS || !input) {
			build_rpc_response(
				req, NULL, SR_ERR_INVAL_ARG,
				&out_status, &out_body, &out_len);
			callback(out_status, out_body,
			         out_len, user_data);
			/* NOTE: out_body est libéré par le callback */
			return;
		}
	}

	if (req->username) {
		sr_session_set_user(
			ctx->sess_running, req->username);
	}

	/* Invoquer le RPC via sysrepo (bloquant mais rapide SHM) */
	rc = sr_rpc_send_tree(
		ctx->sess_running, input, 0, &output);

	lyd_free_all(input);

	build_rpc_response(
		req, output, rc, &out_status, &out_body, &out_len);
	callback(out_status, out_body, out_len, user_data);
	/* NOTE: out_body est libéré par le callback (rpc_data_cb) */

	if (output) sr_release_data(output);
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

	/* RFC 8527 Sec 3.1: datastore dynamique ou identityref
	 * non reconnue — non supporté par ce serveur. */
	if (req->datastore == RC_DS_UNKNOWN) {
		callback(400, "invalid-value",
		         "Unknown or unsupported datastore",
		         user_data);
		return;
	}

	/* RFC 8527 Sec 3.2: certains datastores sont en lecture
	 * seule par nature. `operational` est alimenté par les
	 * capteurs/abonnements ; `intended` est calculé à partir
	 * de `running` + les autres sources NMDA. Toute tentative
	 * de modification MUST échouer avec 405 / 
	 * operation-not-supported. */
	if (req->datastore == RC_DS_OPERATIONAL) {
		callback(405, "operation-not-supported",
		         "Cannot edit operational datastore",
		         user_data);
		return;
	}
	if (req->datastore == RC_DS_INTENDED) {
		callback(405, "operation-not-supported",
		         "Cannot edit read-only intended datastore",
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