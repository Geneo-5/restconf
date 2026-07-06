#define _GNU_SOURCE
#define UNUSED __attribute__((unused))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <event2/event.h>
#include "sse_stream.h"
#include "h2c_server.h"

/* Intervalle keep-alive SSE (30 secondes) */
#define SSE_KEEPALIVE_INTERVAL 30

struct sse_stream_s {
	h2c_session_t *session;
	int32_t stream_id;
	h2c_sse_stream_t *h2c_stream;
	struct event *ping_timer;
	struct event_base *base;
};

/**
 * @brief Callback du timer keep-alive.
 * Envoie un commentaire SSE pour maintenir la connexion.
 */
static void keepalive_cb(evutil_socket_t fd UNUSED,
                         short events UNUSED, void *arg)
{
	sse_stream_t *stream = (sse_stream_t *)arg;
	sse_stream_send_ping(stream);
}

sse_stream_t *sse_stream_create(
	h2c_session_t *session, int32_t stream_id,
	struct event_base *base)
{
	sse_stream_t *stream = calloc(1, sizeof(sse_stream_t));
	if (!stream) return NULL;

	stream->session = session;
	stream->stream_id = stream_id;
	stream->base = base;

	/* Ouvrir le flux SSE via h2c_server */
	stream->h2c_stream = h2c_sse_stream_open(session, stream_id);
	if (!stream->h2c_stream) {
		free(stream);
		return NULL;
	}

	/* Configurer le timer keep-alive */
	if (base) {
		stream->ping_timer = event_new(
			base, -1, EV_PERSIST,
			keepalive_cb, stream);
		if (stream->ping_timer) {
			struct timeval tv = {
				SSE_KEEPALIVE_INTERVAL, 0
			};
			event_add(stream->ping_timer, &tv);
		}
	}

	return stream;
}

int sse_stream_push_event(
	sse_stream_t *stream, const char *payload)
{
	if (!stream || !stream->h2c_stream || !payload) return -1;

	/* Formater en SSE : "data: <payload>\n\n" */
	size_t len = strlen(payload);
	char *sse_data = malloc(len + 10);
	if (!sse_data) return -1;

	snprintf(sse_data, len + 10, "data: %s\n\n", payload);

	int rc = h2c_sse_stream_push(
		stream->h2c_stream,
		(uint8_t *)sse_data,
		strlen(sse_data));

	free(sse_data);
	return rc;
}

int sse_stream_send_ping(sse_stream_t *stream)
{
	if (!stream || !stream->h2c_stream) return -1;

	/* Commentaire SSE : ": ping\n\n" */
	const char *ping = ": ping\n\n";
	return h2c_sse_stream_push(
		stream->h2c_stream,
		(uint8_t *)ping,
		strlen(ping));
}

void sse_stream_close(sse_stream_t *stream)
{
	if (!stream) return;

	/* Arrêter le timer keep-alive */
	if (stream->ping_timer) {
		event_del(stream->ping_timer);
		event_free(stream->ping_timer);
	}

	/* Fermer le flux h2c */
	if (stream->h2c_stream) {
		h2c_sse_stream_close(stream->h2c_stream);
	}

	free(stream);
}
