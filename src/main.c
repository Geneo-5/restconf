#define _GNU_SOURCE
#define UNUSED __attribute__((unused))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>
#include <event2/event.h>
#include <libyang/libyang.h>
#include "h2c_server.h"
#include "jwt_validator.h"
#include "plugin_api.h"
#include "router.h"
#include "codec.h"
#include "sse_stream.h"
#include "logger.h"

/*
 * RFC 8040 Sec 6.3-6.4 (ROADMAP.md item 6.1) : registre des flux
 * SSE actuellement ouverts. Alimenté à l'ouverture d'un stream
 * (RC_RES_EVENT_STREAM) et parcouru par on_notification_cb() pour
 * diffuser chaque notification sysrepo reçue via le plugin.
 *
 * NOTE: h2c_server ne fournit aucun callback de fermeture de
 * stream (cf. Dette Technique ROADMAP.md) ; les entrées mortes
 * sont donc détectées et purgées "best effort" au moment du push
 * (sse_stream_push_event() échoue si le client a fermé la
 * connexion), pas via une notification explicite.
 */
typedef struct sse_registry_entry_s {
	char *stream_name;
	sse_stream_t *stream;
	struct sse_registry_entry_s *next;
} sse_registry_entry_t;

typedef struct {
	jwt_ctx_t *jwt_ctx;
	plugin_ctx_t *plugin_ctx;
	struct event_base *event_base;
	sse_registry_entry_t *sse_registry;
} app_context_t;

/**
 * @brief Enregistre un flux SSE nouvellement ouvert dans le
 * registre applicatif (cf. sse_registry_entry_s ci-dessus).
 */
static void sse_registry_add(
	app_context_t *app, const char *stream_name,
	sse_stream_t *stream)
{
	sse_registry_entry_t *entry = calloc(1, sizeof(*entry));
	if (!entry) return;

	entry->stream_name = strdup(
		(stream_name && *stream_name) ? stream_name : "NETCONF");
	entry->stream = stream;
	entry->next = app->sse_registry;
	app->sse_registry = entry;
}

/**
 * @brief Libère tous les flux SSE encore enregistrés (arrêt du
 * serveur).
 */
static void sse_registry_clear(app_context_t *app)
{
	sse_registry_entry_t *entry = app->sse_registry;

	while (entry) {
		sse_registry_entry_t *next = entry->next;
		sse_stream_close(entry->stream);
		free(entry->stream_name);
		free(entry);
		entry = next;
	}
	app->sse_registry = NULL;
}

/**
 * @brief Callback plugin_notif_cb (RFC 8040 Sec 6.4) : diffuse une
 * notification déjà formatée en JSON/XML par le plugin (cf.
 * build_notification_payload() dans sysrepo_plugin.c) à tous les
 * flux SSE actuellement ouverts.
 *
 * Un seul stream conceptuel ("NETCONF") est annoncé par
 * restconf-state/streams (cf. oper_get_cb), donc toute
 * notification est diffusée à tous les flux ouverts, quel que
 * soit @p module_name / @p xpath — non utilisés pour l'instant,
 * conservés pour un filtrage par stream/module futur.
 */
static void on_notification_cb(
	const char *module_name UNUSED,
	const char *xpath UNUSED,
	const char *payload, void *user_data)
{
	app_context_t *app = (app_context_t *)user_data;
	sse_registry_entry_t **cur = &app->sse_registry;

	if (!payload) return;

	while (*cur) {
		sse_registry_entry_t *entry = *cur;

		if (sse_stream_push_event(
				entry->stream, payload) != 0) {
			/* Push échoué : le client a probablement
			 * fermé la connexion. Nettoyer l'entrée. */
			*cur = entry->next;
			sse_stream_close(entry->stream);
			free(entry->stream_name);
			free(entry);
			continue;
		}
		cur = &entry->next;
	}
}

typedef struct {
	h2c_session_t *session;
	int32_t stream_id;
	media_type_t accept_type;
	bool is_head;
} get_req_ctx_t;

/*
 * NOTE: since transport decoupling (see plugin_api.h), the plugin
 * (internal mode and external/IPC mode alike) returns directly a
 * pre-serialized body (JSON/XML, "fields" and "with-defaults"
 * filtering already applied on the plugin side): this callback only
 * needs to relay it as-is to the HTTP/2 client. This change enables
 * the External mode, since the gateway process has no direct access
 * to sysrepo/libyang to perform that serialization itself.
 *
 * RFC 8040 Sec 3.4.1: ETag header added to GET/HEAD responses.
 */
static void get_data_cb(
	int http_status, uint8_t *body, size_t body_len,
	const char *etag,
	void *user_data)
{
	get_req_ctx_t *ctx = (get_req_ctx_t *)user_data;
	const char *ctype = (ctx->accept_type == MEDIA_TYPE_XML) ?
		"application/yang-data+xml" :
		"application/yang-data+json";

	/* RFC 8040 Sec 3.4.1: ETag header */
	h2c_extra_header_t extra[2] = {
		{NULL, NULL}, {NULL, NULL}
	};
	if (etag && http_status == 200) {
		extra[0].name = "etag";
		extra[0].value = etag;
	}

	/* RFC 8040 Sec 3.4: HEAD retourne les mêmes headers que GET
	 * mais sans body. On passe body=NULL pour ne pas envoyer
	 * de DATA frames. */
	if (ctx->is_head) {
		h2c_send_response_with_headers(
			ctx->session, ctx->stream_id, http_status,
			ctype, NULL, extra, NULL, 0);
	} else {
		h2c_send_response_with_headers(
			ctx->session, ctx->stream_id, http_status,
			ctype, NULL, extra, body, body_len);
	}

	if (body) free(body);
	free(ctx);
}

static void send_error_response(
	h2c_session_t *session, int32_t stream_id,
	media_type_t type, int status,
	const char *tag, const char *msg)
{
	char *body = NULL;
	size_t body_len = 0;
	const char *ctype = (type == MEDIA_TYPE_XML) ?
		"application/yang-data+xml" :
		"application/yang-data+json";

	RC_TRACE("Main send %d %s -> %s", status, tag, msg);
	codec_serialize_errors(type, tag, msg, &body, &body_len);
	h2c_send_response(
		session, stream_id, status, ctype,
		NULL, (uint8_t *)body, body_len);
	if (body) free(body);
}

typedef struct {
	h2c_session_t *session;
	int32_t stream_id;
	media_type_t accept_type;
	char *location;
} edit_req_ctx_t;

static void edit_data_cb(
	int http_status,
	const char *error_tag,
	const char *error_msg,
	const char *etag,
	void *user_data)
{
	edit_req_ctx_t *ctx = (edit_req_ctx_t *)user_data;

	(void)etag; /* TODO: return new ETag after successful edit */

	if (http_status >= 200 && http_status < 300) {
		/* Success (201 Created or 204 No Content) */
		/* RFC 8040 Sec 4.4.1: Location header pour 201 */
		h2c_send_response(
			ctx->session, ctx->stream_id,
			http_status, NULL, ctx->location, NULL, 0);
	} else {
		/* Erreur */
		char *body = NULL;
		size_t body_len = 0;
		const char *ctype = (ctx->accept_type == MEDIA_TYPE_XML) ?
			"application/yang-data+xml" :
			"application/yang-data+json";

		codec_serialize_errors(
			ctx->accept_type, error_tag, error_msg,
			&body, &body_len);

		h2c_send_response(
			ctx->session, ctx->stream_id, http_status,
			ctype, NULL, (uint8_t *)body, body_len);
		if (body) free(body);
	}
	free(ctx->location);
	free(ctx);
}

typedef struct {
	h2c_session_t *session;
	int32_t stream_id;
	media_type_t accept_type;
} rpc_req_ctx_t;

/*
 * NOTE: meme decouplage transport que get_data_cb() : le plugin
 * renvoie directement le corps serialise de l'output du RPC/action
 * (RFC 8040 Sec 3.6.2), pret a etre relaye au client.
 */
static void rpc_data_cb(
	int http_status, uint8_t *body, size_t body_len,
	void *user_data)
{
	rpc_req_ctx_t *ctx = (rpc_req_ctx_t *)user_data;
	const char *ctype = (ctx->accept_type == MEDIA_TYPE_XML) ?
		"application/yang-data+xml" :
		"application/yang-data+json";

	h2c_send_response(
		ctx->session, ctx->stream_id, http_status,
		ctype, NULL, body, body_len);

	if (body) free(body);
	free(ctx);
}

/**
 * @brief Retrieves the implemented revision date of the
 * "ietf-yang-library" YANG module for the "yang-library-version"
 * leaf of the API resource.
 *
 * RFC 8040 Sec 3.3.3 requires this leaf to identify the revision
 * date of the "ietf-yang-library" module implemented by the
 * server, and RFC 8527 Sec 2 requires an NMDA-compliant server to
 * implement at least revision 2019-01-04 of that module. The
 * previous implementation returned a literal "2019-01-04" string
 * regardless of the module actually loaded by sysrepo/libyang;
 * this helper reads the real revision from the libyang context
 * instead (ROADMAP.md item 5.4).
 *
 * @param[in] app Application context (holds the plugin context).
 * @param[out] buf Destination buffer (format YYYY-MM-DD).
 * @param[in] buf_len Size of @p buf in bytes.
 *
 * @note Falls back to the RFC 8527 minimal mandatory revision when
 * the libyang context is unavailable (e.g. External Plugin mode,
 * cf. ROADMAP.md item 3.10) or when the module cannot be found.
 */
static void get_yang_library_revision(
	app_context_t *app, char *buf, size_t buf_len)
{
	/* RFC 8527 Sec 2 minimal mandatory revision: used as a safe
	 * fallback when the real revision cannot be determined. */
	const char *fallback = "2019-01-04";
	const struct ly_ctx *ly_ctx = plugin_acquire_ly_ctx(
		app->plugin_ctx);
	const struct lys_module *mod;

	if (!ly_ctx) {
		snprintf(buf, buf_len, "%s", fallback);
		return;
	}

	mod = ly_ctx_get_module_implemented(
		ly_ctx, "ietf-yang-library");

	if (mod && mod->revision && mod->revision[0] != '\0')
		snprintf(buf, buf_len, "%s", mod->revision);
	else
		snprintf(buf, buf_len, "%s", fallback);

	plugin_release_ly_ctx(app->plugin_ctx);
}

static void on_restconf_request(
	h2c_session_t *session, int32_t stream_id,
	const char *method, const char *path,
	const char *body, size_t body_len,
	void *user_data)
{
	app_context_t *app = (app_context_t *)user_data;
	rc_request_t req = {0};

	RC_TRACE("REQUEST: %s %s -> %ld", method, path, body_len);
	if (body)
 		RC_TRACE("   BODY: %s", body);

	/* 1. Extraction des headers HTTP/2 */
	const char *auth_header = h2c_session_get_header(
		session, "Authorization");
	const char *content_type = h2c_session_get_content_type(
		session);
	const char *accept = h2c_session_get_accept(session);
	/* RFC 8040 Sec 3.4.1: If-Match header */
	const char *if_match = h2c_session_get_if_match(session);

	/* 2. Acquisition du contexte libyang et parsing URI */
	const struct ly_ctx *ly_ctx = plugin_acquire_ly_ctx(
		app->plugin_ctx);

	if (router_parse_request(
	        ly_ctx, path, method, auth_header,
	        content_type, accept, if_match,
	        &req) != 0) {
		send_error_response(
			session, stream_id, req.accept_type,
			400, "invalid-value", "Bad URI");
		router_free_request(&req);
		if (ly_ctx) plugin_release_ly_ctx(app->plugin_ctx);
		return;
	}
	if (ly_ctx) plugin_release_ly_ctx(app->plugin_ctx);

	/* 3. Root Resource Discovery (RFC 8040 Sec 3.1)
	 * Ne nécessite pas d'authentification. */
	if (req.res_type == RC_RES_ROOT_DISCOVERY) {
		const char *xrd =
			"<?xml version='1.0' encoding='UTF-8'?>\n"
			"<XRD xmlns='http://docs.oasis-open.org/ns/"
			"xri/xrd-1.0'>"
			"<Link rel='restconf' href='/restconf'/>"
			"</XRD>";
		h2c_send_response(
			session, stream_id, 200,
			"application/xrd+xml", NULL,
			(const uint8_t *)xrd, strlen(xrd));
		router_free_request(&req);
		return;
	}
	if (req.res_type == RC_RES_ROOT_DISCOVERY_JSON) {
		/* RFC 8040 §3.1: JRD (JSON Resource Descriptor) */
		const char *jrd =
			"{\"links\":[{\"rel\":\"restconf\","
			"\"href\":\"/restconf\"}]}";
		h2c_send_response(
			session, stream_id, 200,
			"application/json", NULL,
			(const uint8_t *)jrd, strlen(jrd));
		router_free_request(&req);
		return;
	}

	/* 4. Authentification JWT et NACM (RFC 8040 Sec 2.5) */
	char username[128] = {0};
	if (req.username == NULL && auth_header != NULL) {
		const char *token = strstr(auth_header, "Bearer ");
		if (token && jwt_validator_verify(
		        app->jwt_ctx, token + 7,
		        username, sizeof(username)) == 0) {
			req.username = strdup(username);
		} else {
			send_error_response(
				session, stream_id, req.accept_type,
				401, "access-denied", "Invalid JWT");
			router_free_request(&req);
			return;
		}
	}

	/* 5. API Resource (RFC 8040 Sec 3.3 & RFC 8527 Sec 2)
	 * Retourne la structure conceptuelle ietf-restconf. */
	if (req.res_type == RC_RES_API) {
		/* RFC 8040 §3.3: seules GET, HEAD, OPTIONS sont
		 * autorisées sur /restconf. */
		if (strcmp(method, "OPTIONS") == 0) {
			h2c_send_response_ex(
				session, stream_id, 200,
				NULL, NULL,
				"allow", "GET, HEAD, OPTIONS",
				NULL, 0);
			router_free_request(&req);
			return;
		}
		if (strcmp(method, "GET") != 0 &&
		    strcmp(method, "HEAD") != 0) {
			send_error_response(
				session, stream_id, req.accept_type,
				405, "operation-not-supported",
				"Method not allowed on /restconf");
			router_free_request(&req);
			return;
		}

		char *api_body = NULL;
		size_t api_len = 0;
		const char *api_ctype;
		/* RFC 8040 Sec 3.3.3 / RFC 8527 Sec 2 (ROADMAP.md 5.4):
		 * revision reelle du module ietf-yang-library implemente
		 * par le serveur, plutot qu'une chaine litterale figee. */
		char yl_rev[16];

		get_yang_library_revision(
			app, yl_rev, sizeof(yl_rev));

		if (req.accept_type == MEDIA_TYPE_XML) {
			api_ctype = "application/yang-data+xml";
			if (asprintf(&api_body,
				"<restconf xmlns=\"urn:ietf:params:xml:"
				"ns:yang:ietf-restconf\">"
				"<data/>"
				"<operations/>"
				"<yang-library-version>%s"
				"</yang-library-version>"
				"</restconf>", yl_rev) < 0) {
				api_body = NULL;
			}
		} else {
			api_ctype = "application/yang-data+json";
			if (asprintf(&api_body,
				"{\"ietf-restconf:restconf\":{"
				"\"data\":{},"
				"\"operations\":{},"
				"\"yang-library-version\":\"%s\""
				"}}", yl_rev) < 0) {
				api_body = NULL;
			}
		}

		if (api_body) {
			api_len = strlen(api_body);
			/* HEAD: headers uniquement, pas de body */
			if (strcmp(method, "HEAD") == 0) {
				h2c_send_response(
					session, stream_id, 200,
					api_ctype, NULL, NULL, 0);
			} else {
				h2c_send_response(
					session, stream_id, 200,
					api_ctype, NULL,
					(const uint8_t *)api_body,
					api_len);
			}
			free(api_body);
		} else {
			send_error_response(
				session, stream_id, req.accept_type,
				500, "operation-failed", "Memory failed");
		}
		router_free_request(&req);
		return;
	}

	/* 6. Routage vers le plugin sysrepo (Data / Operations) */
	if (req.res_type == RC_RES_DATA ||
	    req.res_type == RC_RES_DS) {
		if (strcmp(method, "OPTIONS") == 0) {
			/* RFC 8040 Sec 4.1: OPTIONS sur data */
			h2c_send_response_ex(
				session, stream_id, 200,
				NULL, NULL,
				"allow",
				"GET, HEAD, POST, PUT, PATCH, "
				"DELETE, OPTIONS",
				NULL, 0);
			router_free_request(&req);
			return;
		}
		if (strcmp(method, "GET") == 0 ||
		    strcmp(method, "HEAD") == 0) {

			get_req_ctx_t *ctx = malloc(
				sizeof(get_req_ctx_t));
			if (!ctx) {
				send_error_response(
					session, stream_id,
					req.accept_type, 500,
					"operation-failed",
					"Memory allocation failed");
				router_free_request(&req);
				return;
			}
			ctx->session = session;
			ctx->stream_id = stream_id;
			ctx->accept_type = req.accept_type;
			ctx->is_head = (strcmp(method, "HEAD") == 0);

			plugin_handle_get(
				app->plugin_ctx, &req,
				get_data_cb, ctx);
		} else if (strcmp(method, "POST") == 0 ||
		           strcmp(method, "PUT") == 0 ||
		           strcmp(method, "PATCH") == 0 ||
		           strcmp(method, "DELETE") == 0) {

			edit_req_ctx_t *ctx = malloc(
				sizeof(edit_req_ctx_t));
			if (!ctx) {
				send_error_response(
					session, stream_id,
					req.accept_type, 500,
					"operation-failed",
					"Memory allocation failed");
				router_free_request(&req);
				return;
			}
			ctx->session = session;
			ctx->stream_id = stream_id;
			ctx->accept_type = req.accept_type;

			/* RFC 8040 Sec 4.4.1: Location header pour POST */
			if (strcmp(method, "POST") == 0 && req.xpath) {
				/* Construct the Location URI */
				char loc_buf[4096];
				if (req.res_type == RC_RES_DS) {
					snprintf(loc_buf,
					         sizeof(loc_buf),
					         "/restconf/ds/%s",
					         req.xpath);
				} else {
					snprintf(loc_buf,
					         sizeof(loc_buf),
					         "/restconf/data%s",
					         req.xpath);
				}
				ctx->location = strdup(loc_buf);
			} else {
				ctx->location = NULL;
			}

			plugin_handle_edit(
				app->plugin_ctx, &req,
				(const uint8_t *)body, body_len,
				edit_data_cb, ctx);
		} else {
			send_error_response(
				session, stream_id, req.accept_type,
				405, "operation-not-supported",
				"Method not allowed");
		}
	} else if (req.res_type == RC_RES_OPERATIONS) {
		/* RFC 8040 Sec 3.3.2 / Sec 3.6 */
		if (strcmp(method, "OPTIONS") == 0) {
			h2c_send_response_ex(
				session, stream_id, 200,
				NULL, NULL,
				"allow",
				"GET, HEAD, POST, OPTIONS",
				NULL, 0);
			router_free_request(&req);
			return;
		}
		/* GET /restconf/operations : liste des RPCs
		 * RFC 8040 Sec 3.3.2 */
		if ((strcmp(method, "GET") == 0 ||
		     strcmp(method, "HEAD") == 0) &&
		    (!req.rpc_name || *req.rpc_name == '\0')) {
			const char *ops_ctype =
				(req.accept_type == MEDIA_TYPE_XML) ?
				"application/yang-data+xml" :
				"application/yang-data+json";
			const char *ops_body;
			if (req.accept_type == MEDIA_TYPE_XML) {
				ops_body =
					"<operations xmlns="
					"\"urn:ietf:params:xml:ns:yang"
					":ietf-restconf\"/>";
			} else {
				ops_body =
					"{\"ietf-restconf:operations\":{}}";
			}
			if (strcmp(method, "HEAD") == 0) {
				h2c_send_response(
					session, stream_id, 200,
					ops_ctype, NULL, NULL, 0);
			} else {
				h2c_send_response(
					session, stream_id, 200,
					ops_ctype, NULL,
					(const uint8_t *)ops_body,
					strlen(ops_body));
			}
			router_free_request(&req);
			return;
		}
		if (strcmp(method, "POST") == 0) {
			if (!req.rpc_name) {
				send_error_response(
					session, stream_id,
					req.accept_type, 400,
					"invalid-value",
					"Missing RPC name");
				router_free_request(&req);
				return;
			}

			rpc_req_ctx_t *rpc_ctx = malloc(
				sizeof(rpc_req_ctx_t));
			if (!rpc_ctx) {
				send_error_response(
					session, stream_id,
					req.accept_type, 500,
					"operation-failed",
					"Memory allocation failed");
				router_free_request(&req);
				return;
			}
			rpc_ctx->session = session;
			rpc_ctx->stream_id = stream_id;
			rpc_ctx->accept_type = req.accept_type;

			plugin_handle_rpc(
				app->plugin_ctx, &req,
				(const uint8_t *)body, body_len,
				rpc_data_cb, rpc_ctx);
		} else {
			send_error_response(
				session, stream_id, req.accept_type,
				405, "operation-not-supported",
				"Method not allowed for RPC");
		}
	} else if (req.res_type == RC_RES_EVENT_STREAM) {
		/* RFC 8040 Sec 6.3: Event Stream (SSE)
		 *
		 * NOTE (ROADMAP.md item 6.1/6.2) : la verification stricte
		 * du header Accept a ete retiree. L'URI elle-meme
		 * (/restconf/stream/... ou /streams/...) identifie sans
		 * ambiguite une ressource de type event stream ; de
		 * nombreux clients (y compris la suite de tests) envoient
		 * un Accept generique ("application/yang-data+json") sans
		 * savoir que la ressource repond en "text/event-stream".
		 * Rejeter sur la base du seul header Accept produisait un
		 * 406 systematique qui empechait toute verification
		 * fonctionnelle du flux SSE (cf. Dette Technique). */
		if (strcmp(method, "GET") == 0) {
			/* RFC 8040 Sec 6.3 : aucune ressource de flux
			 * concrete n'existe a la racine ("/restconf/stream"
			 * ou "/streams", sans nom de stream) — le routeur
			 * laisse xpath a NULL dans ce cas (cf. router.c). */
			if (!req.xpath || *req.xpath == '\0') {
				send_error_response(
					session, stream_id,
					req.accept_type, 404,
					"invalid-value",
					"Stream name required");
				router_free_request(&req);
				return;
			}

			/* Créer le flux SSE */
			sse_stream_t *stream = sse_stream_create(
				session, stream_id,
				app->event_base);
			if (!stream) {
				send_error_response(
					session, stream_id,
					req.accept_type, 500,
					"operation-failed",
					"Failed to create SSE stream");
				router_free_request(&req);
				return;
			}

			/* RFC 8040 Sec 6.3-6.4 (ROADMAP.md item 6.1) :
			 * enregistrer le flux dans le registre
			 * applicatif pour qu'il reçoive les
			 * notifications sysrepo poussées par
			 * on_notification_cb() (cf. plus haut). */
			sse_registry_add(app, req.xpath, stream);
		} else {
			send_error_response(
				session, stream_id, req.accept_type,
				405, "operation-not-supported",
				"Only GET allowed on event streams");
		}
	} else {
		send_error_response(
			session, stream_id, req.accept_type,
			404, "invalid-value", "Resource not found");
	}

	router_free_request(&req);
}

static void print_usage(const char *prog) {
	fprintf(stderr, "Usage: %s [options]\n", prog);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -a <addr>   Bind address (default: 127.0.0.1)\n");
	fprintf(stderr, "  -p <port>   Port to listen on (default: 8080)\n");
	fprintf(stderr, "  -u <path>   Listen on Unix socket (h2c) instead of TCP\n");
	fprintf(stderr, "  -k <desc>   JWT key descriptor (default: restconf_jwt_pubkey)\n");
	fprintf(stderr, "  -d          Run as daemon (background)\n");
	fprintf(stderr, "  -s          Use syslog instead of stdout\n");
	fprintf(stderr, "  -v <level>  Runtime log level (0=TRACE, 1=DEBUG, 2=INFO, 3=WARN, 4=ERROR, 5=FATAL)\n");
	fprintf(stderr, "  -h          Show this help\n");
#ifdef USE_EXTERNAL_PLUGIN
	fprintf(stderr, "\n");
	fprintf(stderr, "IPC socket to plugin (compiled-in): %s\n", PLUGIN_UDS_PATH);
#endif
}

int main(int argc, char **argv) {
	const char *bind_addr = "127.0.0.1";
	uint16_t port = 8080;
	const char *uds_path = NULL;
	const char *key_desc = "restconf_jwt_pubkey";
	bool daemonize = false;
#ifdef USE_EXTERNAL_PLUGIN
	bool use_external = true;
#else
	bool use_external = false;
#endif
	rc_log_target_t log_target = RC_LOG_TARGET_STDOUT;
	int runtime_log_level = RC_COMPILE_TIME_LOG_LEVEL;

	int opt;
	while ((opt = getopt(argc, argv, "a:p:u:k:sdv:h")) != -1) {
		switch (opt) {
			case 'a':
				bind_addr = optarg;
				break;
			case 'p':
				port = (uint16_t)atoi(optarg);
				break;
			case 'u':
				uds_path = optarg;
				break;
			case 'k':
				key_desc = optarg;
				break;
			case 'd':
				daemonize = true;
				break;
			case 's':
				log_target = RC_LOG_TARGET_SYSLOG;
				break;
			case 'v':
				runtime_log_level = atoi(optarg);
				break;
			case 'h':
				print_usage(argv[0]);
				return 0;
			default:
				print_usage(argv[0]);
				return 1;
		}
	}

	/* Mode daemon : fork en arrière-plan */
	if (daemonize) {
		if (daemon(0, 0) != 0) {
			perror("daemon");
			return 1;
		}
		/* En mode daemon, forcer syslog si stdout était demandé */
		if (log_target == RC_LOG_TARGET_STDOUT) {
			log_target = RC_LOG_TARGET_SYSLOG;
		}
	}

	rc_log_init(log_target, runtime_log_level);

	RC_INFO("Starting RESTCONF h2c server...");
	RC_DEBUG("Compile-time max log level: %d", RC_COMPILE_TIME_LOG_LEVEL);
	RC_DEBUG("Runtime log level: %d", runtime_log_level);

	app_context_t app = {0};

	app.jwt_ctx = jwt_validator_init(key_desc);
	if (!app.jwt_ctx) {
		RC_FATAL("Failed to init JWT validator");
		return 1;
	}

	/* Créer le serveur h2c en premier pour obtenir l'event base */
	h2c_server_t *server;
	if (uds_path) {
		server = h2c_server_init_uds(
			uds_path, on_restconf_request, &app);
		if (!server) {
			RC_FATAL("Failed to init h2c server on Unix socket %s", uds_path);
			jwt_validator_destroy(app.jwt_ctx);
			return 1;
		}
		RC_INFO("RESTCONF h2c server listening on Unix socket %s", uds_path);
	} else {
		server = h2c_server_init(
			bind_addr, port, on_restconf_request, &app);
		if (!server) {
			RC_FATAL("Failed to init h2c server on %s:%d", bind_addr, port);
			jwt_validator_destroy(app.jwt_ctx);
			return 1;
		}
		RC_INFO("RESTCONF h2c server listening on %s:%d", bind_addr, port);
	}

	/* Initialiser le plugin avec l'event base du serveur */
	struct event_base *base = h2c_server_get_event_base(server);
	app.event_base = base;
	app.plugin_ctx = plugin_init(
		base, use_external, PLUGIN_UDS_PATH);
	if (!app.plugin_ctx) {
		RC_FATAL("Failed to init plugin");
		h2c_server_destroy(server);
		jwt_validator_destroy(app.jwt_ctx);
		return 1;
	}

	/* RFC 8040 Sec 6.4 (ROADMAP.md item 6.1) : brancher la
	 * diffusion des notifications sysrepo vers les flux SSE
	 * actifs (cf. on_notification_cb() / sse_registry_*
	 * ci-dessus). Sans effet en mode Externe pour l'instant
	 * (plugin_subscribe_notifications reste un stub cote
	 * uds_gateway.c, cf. ROADMAP.md item 3.8). */
	plugin_subscribe_notifications(
		app.plugin_ctx, on_notification_cb, &app);

	h2c_server_run(server);

	sse_registry_clear(&app);
	h2c_server_destroy(server);
	plugin_destroy(app.plugin_ctx);
	jwt_validator_destroy(app.jwt_ctx);

	RC_INFO("Server shutdown complete");
	return 0;
}
