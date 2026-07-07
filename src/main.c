#define _GNU_SOURCE
#define UNUSED __attribute__((unused))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>
#include <event2/event.h>
#include "h2c_server.h"
#include "jwt_validator.h"
#include "plugin_api.h"
#include "router.h"
#include "codec.h"
#include "sse_stream.h"
#include "logger.h"

typedef struct {
	jwt_ctx_t *jwt_ctx;
	plugin_ctx_t *plugin_ctx;
	struct event_base *event_base;
} app_context_t;

typedef struct {
	h2c_session_t *session;
	int32_t stream_id;
	media_type_t accept_type;
	char *with_defaults;
	char *fields_expr;
} get_req_ctx_t;

static void get_data_cb(
	sr_data_t *data, int error_code, void *user_data)
{
	get_req_ctx_t *ctx = (get_req_ctx_t *)user_data;
	char *body = NULL;
	size_t body_len = 0;
	int status = 200;
	const char *ctype = (ctx->accept_type == MEDIA_TYPE_XML) ?
		"application/yang-data+xml" :
		"application/yang-data+json";

	if (error_code != SR_ERR_OK) {
		const char *tag = "operation-failed";
		const char *msg = sr_strerror(error_code);

		/* RFC 8527 Sec 3.1/3.2: identityref de datastore
		 * inconnue ou non supportée (dynamique). */
		if (error_code == SR_ERR_INVAL_ARG) {
			status = 400;
			tag = "invalid-value";
			msg = "Unknown or unsupported datastore";
		} else if (error_code == SR_ERR_NOT_FOUND) {
			status = 404;
			tag = "invalid-value";
		} else if (error_code == SR_ERR_UNAUTHORIZED) {
			status = 403;
			tag = "access-denied";
		} else {
			status = 500;
		}
		codec_serialize_errors(
			ctx->accept_type, tag, msg,
			&body, &body_len);
	} else if (data && data->tree) {
		struct lyd_node *filtered = NULL;

		/* Appliquer le filtre fields si présent */
		if (ctx->fields_expr) {
			if (codec_filter_fields(
			        data->tree,
			        ctx->fields_expr,
			        &filtered) == 0
			    && filtered) {
				if (codec_serialize_data_wd(
				        filtered, ctx->accept_type,
				        ctx->with_defaults,
				        &body, &body_len) != 0) {
					status = 500;
					codec_serialize_errors(
						ctx->accept_type,
						"operation-failed",
						"Serialization failed",
						&body, &body_len);
				}
				lyd_free_all(filtered);
			} else {
				/* Filtre vide ou erreur :
				 * renvoyer l'arbre original */
				if (codec_serialize_data_wd(
				        data->tree,
				        ctx->accept_type,
				        ctx->with_defaults,
				        &body, &body_len) != 0) {
					status = 500;
					codec_serialize_errors(
						ctx->accept_type,
						"operation-failed",
						"Serialization failed",
						&body, &body_len);
				}
			}
		} else {
			if (codec_serialize_data_wd(
			        data->tree, ctx->accept_type,
			        ctx->with_defaults,
			        &body, &body_len) != 0) {
				status = 500;
				codec_serialize_errors(
					ctx->accept_type,
					"operation-failed",
					"Serialization failed",
					&body, &body_len);
			}
		}
	} else {
		status = 204; 
	}

	h2c_send_response(
		ctx->session, ctx->stream_id, status,
		ctype, NULL, (uint8_t *)body, body_len);

	if (body) free(body);
	if (data) sr_release_data(data);
	free(ctx->with_defaults);
	free(ctx->fields_expr);
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
	void *user_data)
{
	edit_req_ctx_t *ctx = (edit_req_ctx_t *)user_data;
	
	if (http_status >= 200 && http_status < 300) {
		/* Succès (201 Created ou 204 No Content) */
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

static void rpc_data_cb(
	sr_data_t *output, int error_code, void *user_data)
{
	rpc_req_ctx_t *ctx = (rpc_req_ctx_t *)user_data;
	char *body = NULL;
	size_t body_len = 0;
	int status = 200;
	const char *ctype = (ctx->accept_type == MEDIA_TYPE_XML) ?
		"application/yang-data+xml" :
		"application/yang-data+json";

	if (error_code != SR_ERR_OK) {
		const char *tag = "operation-failed";
		const char *msg = sr_strerror(error_code);

		if (error_code == SR_ERR_UNAUTHORIZED) {
			status = 403;
			tag = "access-denied";
		} else if (error_code == SR_ERR_NOT_FOUND) {
			status = 404;
			tag = "invalid-value";
		} else if (error_code == SR_ERR_INVAL_ARG) {
			status = 400;
			tag = "invalid-value";
		} else if (error_code ==
		           SR_ERR_VALIDATION_FAILED) {
			status = 400;
			tag = "invalid-value";
		} else {
			status = 500;
		}
		codec_serialize_errors(
			ctx->accept_type, tag, msg,
			&body, &body_len);
	} else if (output && output->tree) {
		/* Sérialiser l'output du RPC */
		if (codec_serialize_data(
	        output->tree, ctx->accept_type,
	        &body, &body_len) != 0) {
			status = 500;
			codec_serialize_errors(
				ctx->accept_type,
				"operation-failed",
				"RPC output serialization failed",
				&body, &body_len);
		}
	}
	/* Si output est NULL et pas d'erreur, le RPC n'a
	 * pas d'output — renvoyer 204 No Content */
	if (error_code == SR_ERR_OK && !output) {
		status = 204;
	}

	h2c_send_response(
		ctx->session, ctx->stream_id, status,
		ctype, NULL, (uint8_t *)body, body_len);

	if (body) free(body);
	if (output) sr_release_data(output);
	free(ctx);
}

static void on_restconf_request(
	h2c_session_t *session, int32_t stream_id,
	const char *method, const char *path,
	const char *body, size_t body_len,
	void *user_data)
{
	app_context_t *app = (app_context_t *)user_data;
	rc_request_t req = {0};
	
	/* 1. Extraction des headers HTTP/2 */
	const char *auth_header = h2c_session_get_header(
		session, "Authorization");
	const char *content_type = h2c_session_get_content_type(
		session);
	const char *accept = h2c_session_get_accept(session);
	
	/* 2. Acquisition du contexte libyang et parsing URI */
	const struct ly_ctx *ly_ctx = plugin_acquire_ly_ctx(
		app->plugin_ctx);
	
	if (router_parse_request(
	        ly_ctx, path, method, auth_header,
	        content_type, accept, &req) != 0) {
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
		char *api_body = NULL;
		size_t api_len = 0;
		const char *api_ctype;

		if (req.accept_type == MEDIA_TYPE_XML) {
			api_ctype = "application/yang-data+xml";
			api_body = strdup(
				"<restconf xmlns=\"urn:ietf:params:xml:"
				"ns:yang:ietf-restconf\">"
				"<data/>"
				"<operations/>"
				"<yang-library-version>2019-01-04"
				"</yang-library-version>"
				"</restconf>");
		} else {
			api_ctype = "application/yang-data+json";
			api_body = strdup(
				"{\"ietf-restconf:restconf\":{"
				"\"data\":{},"
				"\"operations\":{},"
				"\"yang-library-version\":\"2019-01-04\""
				"}}");
		}

		if (api_body) {
			api_len = strlen(api_body);
			h2c_send_response(
				session, stream_id, 200, api_ctype,
				NULL, (const uint8_t *)api_body, api_len);
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
		if (strcmp(method, "GET") == 0 ||
		    strcmp(method, "HEAD") == 0) {
			
			get_req_ctx_t *ctx = malloc(
				sizeof(get_req_ctx_t));
			ctx->session = session;
			ctx->stream_id = stream_id;
			ctx->accept_type = req.accept_type;
			ctx->with_defaults = req.with_defaults ?
				strdup(req.with_defaults) : NULL;
			ctx->fields_expr = req.fields_expr ?
				strdup(req.fields_expr) : NULL;
			
			plugin_handle_get(
				app->plugin_ctx, &req,
				get_data_cb, ctx);
		} else if (strcmp(method, "POST") == 0 ||
		           strcmp(method, "PUT") == 0 ||
		           strcmp(method, "PATCH") == 0 ||
		           strcmp(method, "DELETE") == 0) {
			
			edit_req_ctx_t *ctx = malloc(
				sizeof(edit_req_ctx_t));
			ctx->session = session;
			ctx->stream_id = stream_id;
			ctx->accept_type = req.accept_type;

			/* RFC 8040 Sec 4.4.1: Location header pour POST */
			if (strcmp(method, "POST") == 0 && req.xpath) {
				/* Construire l'URI Location */
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
		/* RFC 8040 Sec 3.6: Invocation RPC/Action */
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
		/* RFC 8040 Sec 6.3: Event Stream (SSE) */
		if (strcmp(method, "GET") == 0) {
			/* Vérifier le header Accept */
			const char *accept = h2c_session_get_accept(
				session);
			if (accept && strstr(accept,
			                    "text/event-stream") == NULL) {
				send_error_response(
					session, stream_id,
					req.accept_type, 406,
					"operation-not-supported",
					"Accept must be "
					"text/event-stream");
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

			/* TODO: Abonner le stream aux notifications
			 * sysrepo correspondantes via le plugin.
			 * Pour l'instant, le flux reste ouvert avec
			 * keep-alive uniquement. */
			(void)stream; /* Le stream reste actif */
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
	
	h2c_server_run(server);

	h2c_server_destroy(server);
	plugin_destroy(app.plugin_ctx);
	jwt_validator_destroy(app.jwt_ctx);

	RC_INFO("Server shutdown complete");
	return 0;
}