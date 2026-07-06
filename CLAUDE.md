# CLAUDE.md - Project Guidelines for RESTCONF h2c Backend

## 📖 Project Overview
This project is a high-performance, strictly single-threaded, and 100% asynchronous RESTCONF backend server written in C. It implements **RFC 8040** (RESTCONF), **RFC 8527** (NMDA extensions), and **RFC 8650** (Subscribed Notifications via `ietf-restconf-subscribed-notifications`). 

It operates exclusively over **HTTP/2 Cleartext (h2c)**, delegating TLS termination to a reverse proxy. Authentication is handled via JWT, with cryptographic verification relying on the **Linux Kernel Keyring**. The business logic is implemented as an **internal or external sysrepo plugin** (configurable via CMake for privilege separation).

## 🚨 CRITICAL CONSTRAINTS (THE "GOLDEN RULES")
**Violating these rules will break the architecture. AI agents MUST adhere to them strictly.**

1. **NO THREADS (`pthread`, `stdthread`, etc. are FORBIDDEN):** The entire application must run in a single thread driven by the `libevent` event loop. 
2. **NO BLOCKING CALLS:** Never block the `libevent` loop. All I/O, network operations, and sysrepo IPC must be asynchronous.
3. **NO TLS/HTTPS:** The backend speaks **h2c only**. Do not implement OpenSSL TLS server logic. Trust the reverse proxy for TLS.
4. **DUAL-MODE PLUGIN:** The sysrepo plugin must be compilable as an internal (in-process) or external (out-of-process via UDS) daemon using the CMake option `BUILD_EXTERNAL_PLUGIN`.
5. **NO LIBKEYUTILS:** Do not use the `libkeyutils` library. You MUST use raw Linux syscalls (`syscall(__NR_request_key, ...)`, `syscall(__NR_keyctl, ...)`) to interact with the Kernel Keyring. Include `<linux/keyctl.h>` and `<sys/syscall.h>`.

## 📏 Code Style & Formatting (STRICT)
**All generated C code and scripts MUST strictly follow these formatting rules:**
- **Indentation:** You MUST use **TAB** characters for indentation. The tab width is visually set to **8**. **NEVER** use spaces for indentation.
- **Line Length:** The maximum line length is strictly **80 characters**. You MUST wrap and break lines to respect this limit. Do not exceed 80 columns.
- **Align:** align start with **TAB** until indent length and finish with **SPACE**
```c
#define FIX_SAMPLE_PACKED_SIZE_MAX (DPACK_UINT8_SIZE_MAX + \
                                    DPACK_UINT16_SIZE_MAX + \
                                    DPACK_UINT32_SIZE_MAX)

extern int
map_sample_pack(struct dpack_encoder    * encoder,
                const struct map_sample * sample);
```

## 🏗️ Architecture & Tech Stack

- **Event Loop:** `libevent` (Master of all I/O and timers).
- **HTTP/2 Engine:** `nghttp2` (Configured for h2c, Prior Knowledge or Upgrade).
- **Datastore:** `sysrepo` & `libyang` (YANG data modeling, NMDA datastores).
- **Security:** `libkeyutils` (Kernel Keyring) + `OpenSSL` (In-memory JWT crypto verification).
- **Language:** C11, CMake build system.

### Data Flow & Event Integration
- `libevent` listens on a TCP socket.
- `nghttp2` parses HTTP/2 frames.
- `sysrepo` file descriptors (obtained via `sr_get_event_fd()`) are registered in `libevent` using `event_new(..., EV_READ)`.
- When `sysrepo` has data/notifications, the FD triggers a `libevent` callback, which calls `sr_subscription_process_events()` **inside the main thread**.

## 💻 Coding Guidelines & Patterns

### 1. Sysrepo Integration (Async & FD Mapping)
Always prefer asynchronous sysrepo APIs to prevent blocking the event loop during IPC.
```c
/* GOOD: Asynchronous data retrieval */
sr_get_data_async(session, xpath, 0, 0, 0, my_data_cb, req_ctx);

/* BAD: Synchronous blocking call in the main loop */
sr_get_data(session, xpath, 0, 0, 0, &data); 

## Syrepo sample code

```c
#define _GNU_SOURCE
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

/* Contexte pour le callback asynchrone GET */
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
		/* TODO: Sérialiser data en JSON via libyang */
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
	const char *body, size_t body_len, void *user_data)
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
			
			/* Appel asynchrone correct */
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

int main(int argc, char **argv) {
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
```
