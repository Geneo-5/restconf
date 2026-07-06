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

typedef struct {
	jwt_ctx_t *jwt_ctx;
	plugin_ctx_t *plugin_ctx;
} app_context_t;

typedef struct {
	h2c_session_t *session;
	int32_t stream_id;
} get_req_ctx_t;

static void get_data_cb(
	sr_data_t *data, int error_code, void *user_data)
{
	get_req_ctx_t *ctx = (get_req_ctx_t *)user_data;
	if (error_code != SR_ERR_OK) {
		h2c_send_response(
			ctx->session, ctx->stream_id, 500,
			"application/yang-data+json",
			(uint8_t *)"{\"ietf-restconf:errors\":{\"error\":[{\"error-tag\":\"operation-failed\"}]}}",
			82);
	} else {
		h2c_send_response(
			ctx->session, ctx->stream_id, 200,
			"application/yang-data+json",
			(uint8_t *)"{}", 2);
	}
	if (data) sr_release_data(data);
	free(ctx);
}

static void on_restconf_request(
	h2c_session_t *session, int32_t stream_id,
	const char *method, const char *path,
	const char *body UNUSED, size_t body_len UNUSED,
	void *user_data)
{
	app_context_t *app = (app_context_t *)user_data;
	rc_request_t req = {0};
	
	const char *auth_header = h2c_session_get_header(
		session, "Authorization");
	
	if (router_parse_request(
	        path, method, auth_header, &req) != 0) {
		h2c_send_response(
			session, stream_id, 400,
			"application/yang-data+json",
			(uint8_t *)"{\"ietf-restconf:errors\":{\"error\":[{\"error-tag\":\"invalid-value\"}]}}",
			78);
		return;
	}

	char username[128] = {0};
	if (req.username == NULL && auth_header != NULL) {
		const char *token = strstr(auth_header, "Bearer ");
		if (token && jwt_validator_verify(
		        app->jwt_ctx, token + 7,
		        username, sizeof(username)) == 0) {
			req.username = strdup(username);
		} else {
			h2c_send_response(
				session, stream_id, 401,
				"application/yang-data+json",
				(uint8_t *)"{\"ietf-restconf:errors\":{\"error\":[{\"error-tag\":\"access-denied\"}]}}",
				79);
			router_free_request(&req);
			return;
		}
	}

	if (req.res_type == RC_RES_DATA ||
	    req.res_type == RC_RES_DS) {
		if (strcmp(method, "GET") == 0 ||
		    strcmp(method, "HEAD") == 0) {
			
			get_req_ctx_t *ctx = malloc(sizeof(get_req_ctx_t));
			ctx->session = session;
			ctx->stream_id = stream_id;
			
			plugin_handle_get(
				app->plugin_ctx, &req,
				get_data_cb, ctx);
		} else {
			plugin_handle_edit(
				app->plugin_ctx, &req, NULL, session);
		}
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