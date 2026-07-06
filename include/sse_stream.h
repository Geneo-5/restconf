#ifndef SSE_STREAM_H
#define SSE_STREAM_H

#include <stdint.h>
#include <stdbool.h>
#include <event2/event.h>
#include "h2c_server.h"

typedef struct sse_stream_s sse_stream_t;

/**
 * @brief Crée un nouveau flux SSE associé à un stream HTTP/2.
 * Envoie les headers initiaux (Content-Type: text/event-stream).
 * @param base Event base pour le timer keep-alive.
 */
sse_stream_t *sse_stream_create(
	h2c_session_t *session, int32_t stream_id,
	struct event_base *base);

/**
 * @brief Pousse une notification YANG formatée en SSE vers le client.
 * @note Non-bloquant, utilise la file d'attente de nghttp2.
 * @param payload Données JSON ou XML de la notification.
 */
int sse_stream_push_event(sse_stream_t *stream, const char *payload);

/**
 * @brief Envoie un commentaire de Keep-Alive SSE (": ping\n\n").
 */
int sse_stream_send_ping(sse_stream_t *stream);

/**
 * @brief Ferme le flux SSE (envoie le flag END_STREAM).
 */
void sse_stream_close(sse_stream_t *stream);

#endif // SSE_STREAM_H