#define _GNU_SOURCE
#define UNUSED __attribute__((unused))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <event2/event.h>
#include "h2c_server.h"
#include "jwt_validator.h"
#include "plugin_api.h"
#include "router.h"
#include "codec.h"

typedef struct {
	jwt_ctx_t *jwt_ctx;
	plugin_ctx_t *plugin_ctx;
} app_context_t;

typedef struct {
	h2c_session_t *session;
	int32_t stream_id;
	media_type_t accept_type;
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
		status = 500;
		codec_serialize_errors(
			ctx->accept_type, "operation-failed",
			"Sysrepo operation failed",
			&body, &body_len);
	} else if (data && data->tree) {
		if (codec_serialize_data(
		        data->tree, ctx->accept_type,
		        &body, &body_len) != 0) {
			status = 500;
			codec_serialize_errors(
				ctx->accept_type,
				"operation-failed",
				"Serialization failed",
				&body, &body_len);
		}
	} else {
		status = 204; 
	}

	h2c_send_response(
		ctx->session, ctx->stream_id, status,
		ctype, NULL, (uint8_t *)body, body_len);

	if (body) free(body);
	if (data) sr_release_data(data);
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
		/* TODO: Générer le header Location pour 201 */
		h2c_send_response(
			ctx->session, ctx->stream_id,
			http_status, NULL, NULL, NULL, 0);
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
		/* TODO: Gérer les RPC */
		send_error_response(
			session, stream_id, req.accept_type,
			501, "operation-not-supported",
			"RPC not implemented yet");
	} else {
		send_error_response(
			session, stream_id, req.accept_type,
			404, "invalid-value", "Resource not found");
	}

	router_free_request(&req);
}

int main(int argc UNUSED, char **argv UNUSED) {
	const char *bind_addr = "127.0.0.1";
	uint16_t port = 8080;
	const char *key_desc = "restconf_jwt_pubkey";
	bool use_external = false;

	app_context_t app = {0};

	app.jwt_ctx = jwt_validator_init(key_desc);
	if (!app.jwt_ctx) {
		fprintf(stderr, "Failed to init JWT validator\n");
		return 1;
	}

	app.plugin_ctx = plugin_init(
		NULL, use_external, "/var/run/restconf.sock");
	if (!app.plugin_ctx) {
		fprintf(stderr, "Failed to init plugin\n");
		return 1;
	}

	h2c_server_t *server = h2c_server_init(
		bind_addr, port, on_restconf_request, &app);
	if (!server) {
		fprintf(stderr, "Failed to init h2c server\n");
		return 1;
	}

	printf("RESTCONF h2c server listening on %s:%d\n",
	       bind_addr, port);
	
	h2c_server_run(server);

	h2c_server_destroy(server);
	plugin_destroy(app.plugin_ctx);
	jwt_validator_destroy(app.jwt_ctx);

	return 0;
}