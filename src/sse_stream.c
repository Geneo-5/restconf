#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <event2/event.h>
#include "sse_stream.h"
#include "h2c_server.h"

struct sse_stream_s {
		h2c_session_t *session;
		int32_t stream_id;
		struct event *ping_timer;
};

sse_stream_t *sse_stream_create(
		h2c_session_t *session, int32_t stream_id)
{
		sse_stream_t *stream = calloc(1, sizeof(sse_stream_t));
		if (!stream) return NULL;

		stream->session = session;
		stream->stream_id = stream_id;

		/* Send SSE headers without END_STREAM flag */
		h2c_send_sse_headers(session, stream_id);

		/* TODO: Setup libevent timer for keep-alive pings */
		return stream;
}

int sse_stream_push_event(
		sse_stream_t *stream, const char *payload)
{
		if (!stream || !payload) return -1;

		/* Format as SSE: "data: <payload>\n\n" */
		size_t len = strlen(payload);
		char *sse_data = malloc(len + 10);
		if (!sse_data) return -1;

		snprintf(sse_data, len + 10, "data: %s\n\n", payload);

		/* Push via nghttp2 */
		h2c_send_sse_data(
				stream->session,
				stream->stream_id,
				(uint8_t *)sse_data,
				strlen(sse_data));

		free(sse_data);
		return 0;
}

int sse_stream_send_ping(sse_stream_t *stream)
{
		if (!stream) return -1;
		const char *ping = ": ping\n\n";
		h2c_send_sse_data(
				stream->session,
				stream->stream_id,
				(uint8_t *)ping,
				strlen(ping));
		return 0;
}

void sse_stream_close(sse_stream_t *stream)
{
		if (!stream) return;
		if (stream->ping_timer) {
				event_free(stream->ping_timer);
		}
		/* TODO: Send END_STREAM flag */
		free(stream);
}