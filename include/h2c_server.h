#ifndef H2C_SERVER_H
#define H2C_SERVER_H

#include <event2/event.h>
#include <nghttp2/nghttp2.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct h2c_server_s h2c_server_t;
typedef struct h2c_session_s h2c_session_t;

typedef void (*h2c_request_cb)(
	h2c_session_t *session, int32_t stream_id,
	const char *method, const char *path,
	const char *body, size_t body_len, void *user_data);

h2c_server_t *h2c_server_init(
	const char *bind_addr, uint16_t port,
	h2c_request_cb req_cb, void *user_data);

void h2c_server_run(h2c_server_t *server);
void h2c_server_destroy(h2c_server_t *server);

int h2c_send_response(
	h2c_session_t *session, int32_t stream_id,
	int status_code, const char *content_type,
	const uint8_t *body, size_t body_len);

int h2c_send_sse_headers(h2c_session_t *session, int32_t stream_id);
int h2c_send_sse_data(
	h2c_session_t *session, int32_t stream_id,
	const uint8_t *data, size_t data_len);

struct event_base *h2c_server_get_event_base(h2c_server_t *server);

/* Récupère un header HTTP/2 stocké (ex: Authorization) */
const char *h2c_session_get_header(
	h2c_session_t *session, const char *name);

/**
 * @brief Récupère le header Content-Type de la requête.
 */
const char *h2c_session_get_content_type(
	h2c_session_t *session);

/**
 * @brief Récupère le header Accept de la requête.
 */
const char *h2c_session_get_accept(
	h2c_session_t *session);

#endif // H2C_SERVER_H