#define _GNU_SOURCE
#define UNUSED __attribute__((unused))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <nghttp2/nghttp2.h>
#include "h2c_server.h"

#define MAKE_NV(NAME, VALUE, VALUELEN) \
	((nghttp2_nv){(uint8_t *)(NAME), (uint8_t *)(VALUE), \
				  sizeof(NAME) - 1, VALUELEN, \
				  NGHTTP2_NV_FLAG_NONE})

struct h2c_session_s {
	nghttp2_session *ng_session;
	struct bufferevent *bev;
	h2c_server_t *server;
	char method[16];
	char path[2048];
	char auth_header[4096];
	char content_type[128];
	char accept[128];
	uint8_t *body;
	size_t body_len;
	size_t body_cap;
	int32_t current_stream_id;
};

struct h2c_server_s {
	struct event_base *base;
	struct evconnlistener *listener;
	h2c_request_cb req_cb;
	void *user_data;
};

typedef struct {
	const uint8_t *data;
	size_t len;
	size_t offset;
} data_source_t;

/* SSE data queue for deferred streaming */
typedef struct sse_queue_item {
	uint8_t *data;
	size_t len;
	struct sse_queue_item *next;
} sse_queue_item_t;

/* SSE stream wrapper for h2c + libevent integration */
struct h2c_sse_stream_s {
	h2c_session_t *session;
	int32_t stream_id;
	sse_queue_item_t *head;
	sse_queue_item_t *tail;
	bool deferred; /* true si le callback a renvoyé DEFERRED */
};

static ssize_t data_read_callback(
	nghttp2_session *session UNUSED,
	int32_t stream_id UNUSED,
	uint8_t *buf, size_t length,
	uint32_t *data_flags,
	nghttp2_data_source *source,
	void *user_data UNUSED)
{
	data_source_t *src = (data_source_t *)source->ptr;
	size_t remaining = src->len - src->offset;
	size_t to_copy = remaining < length ? remaining : length;

	memcpy(buf, src->data + src->offset, to_copy);
	src->offset += to_copy;

	if (src->offset == src->len) {
		*data_flags |= NGHTTP2_DATA_FLAG_EOF;
	}
	return (ssize_t)to_copy;
}

const char *h2c_session_get_header(
	h2c_session_t *session, const char *name)
{
	if (!session || !name) return NULL;
	if (strcasecmp(name, "Authorization") == 0) {
		return session->auth_header[0] ?
		       session->auth_header : NULL;
	}
	return NULL;
}

static ssize_t send_callback(
	nghttp2_session *session UNUSED,
	const uint8_t *data, size_t length,
	int flags UNUSED, void *user_data)
{
	h2c_session_t *h2_session = (h2c_session_t *)user_data;
	bufferevent_write(h2_session->bev, data, length);
	return (ssize_t)length;
}

static int on_header_callback(
	nghttp2_session *session UNUSED,
	const nghttp2_frame *frame,
	const uint8_t *name, size_t namelen,
	const uint8_t *value, size_t valuelen,
	uint8_t flags UNUSED, void *user_data)
{
	h2c_session_t *h2_session = (h2c_session_t *)user_data;
	if (frame->hd.type != NGHTTP2_HEADERS) return 0;

	if (namelen == 7 && memcmp(name, ":method", 7) == 0) {
		snprintf(h2_session->method,
		         sizeof(h2_session->method),
		         "%.*s", (int)valuelen, value);
	} else if (namelen == 5 &&
	           memcmp(name, ":path", 5) == 0) {
		snprintf(h2_session->path,
		         sizeof(h2_session->path),
		         "%.*s", (int)valuelen, value);
	} else if (namelen == 13 &&
	           memcmp(name, "authorization", 13) == 0) {
		snprintf(h2_session->auth_header,
		         sizeof(h2_session->auth_header),
		         "%.*s", (int)valuelen, value);
	} else if (namelen == 12 &&
	           memcmp(name, "content-type", 12) == 0) {
		snprintf(h2_session->content_type,
		         sizeof(h2_session->content_type),
		         "%.*s", (int)valuelen, value);
	} else if (namelen == 6 &&
	           memcmp(name, "accept", 6) == 0) {
		snprintf(h2_session->accept,
		         sizeof(h2_session->accept),
		         "%.*s", (int)valuelen, value);
	}
	return 0;
}

static int on_data_chunk_recv_callback(
	nghttp2_session *session UNUSED,
	uint8_t flags UNUSED,
	int32_t stream_id UNUSED,
	const uint8_t *data, size_t len,
	void *user_data)
{
	h2c_session_t *h2_session = (h2c_session_t *)user_data;
	if (h2_session->body_len + len > h2_session->body_cap) {
		h2_session->body_cap = (h2_session->body_len + len) * 2;
		h2_session->body = realloc(
			h2_session->body, h2_session->body_cap);
	}
	memcpy(h2_session->body + h2_session->body_len, data, len);
	h2_session->body_len += len;
	return 0;
}

static int on_frame_recv_callback(
	nghttp2_session *session UNUSED,
	const nghttp2_frame *frame, void *user_data)
{
	h2c_session_t *h2_session = (h2c_session_t *)user_data;
	if (frame->hd.type == NGHTTP2_HEADERS &&
	    (frame->hd.flags & NGHTTP2_FLAG_END_STREAM)) {
		h2_session->server->req_cb(
			h2_session, frame->hd.stream_id,
			h2_session->method, h2_session->path,
			(const char *)h2_session->body,
			h2_session->body_len,
			h2_session->server->user_data);
		h2_session->body_len = 0;
	} else if (frame->hd.type == NGHTTP2_DATA &&
	           (frame->hd.flags & NGHTTP2_FLAG_END_STREAM)) {
		h2_session->server->req_cb(
			h2_session, frame->hd.stream_id,
			h2_session->method, h2_session->path,
			(const char *)h2_session->body,
			h2_session->body_len,
			h2_session->server->user_data);
		h2_session->body_len = 0;
	}
	return 0;
}

static void bev_read_cb(struct bufferevent *bev, void *ctx) {
	h2c_session_t *h2_session = (h2c_session_t *)ctx;
	struct evbuffer *input = bufferevent_get_input(bev);
	uint8_t buf[4096];
	int len;

	while ((len = evbuffer_remove(input, buf, sizeof(buf))) > 0) {
		if (nghttp2_session_mem_recv(
		        h2_session->ng_session, buf, len) < 0) {
			bufferevent_free(bev);
			return;
		}
	}
	nghttp2_session_send(h2_session->ng_session);
}

static void bev_event_cb(
	struct bufferevent *bev, short events, void *ctx)
{
	if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
		h2c_session_t *h2_session = (h2c_session_t *)ctx;
		nghttp2_session_terminate_session(
			h2_session->ng_session, NGHTTP2_NO_ERROR);
		nghttp2_session_del(h2_session->ng_session);
		bufferevent_free(bev);
		free(h2_session->body);
		free(h2_session);
	}
}

static void accept_cb(
	struct evconnlistener *listener UNUSED,
	evutil_socket_t fd,
	struct sockaddr *address UNUSED,
	int socklen UNUSED, void *ctx)
{
	h2c_server_t *server = (h2c_server_t *)ctx;
	struct event_base *base = evconnlistener_get_base(listener);

	h2c_session_t *h2_session = calloc(1, sizeof(h2c_session_t));
	h2_session->server = server;
	h2_session->bev = bufferevent_socket_new(
		base, fd, BEV_OPT_CLOSE_ON_FREE);

	nghttp2_session_callbacks *callbacks;
	nghttp2_session_callbacks_new(&callbacks);
	nghttp2_session_callbacks_set_send_callback(
		callbacks, send_callback);
	nghttp2_session_callbacks_set_on_header_callback(
		callbacks, on_header_callback);
	nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
		callbacks, on_data_chunk_recv_callback);
	nghttp2_session_callbacks_set_on_frame_recv_callback(
		callbacks, on_frame_recv_callback);

	nghttp2_option *opts;
	nghttp2_option_new(&opts);
	nghttp2_option_set_no_auto_window_update(opts, 0);

	nghttp2_session_server_new(
		&h2_session->ng_session, callbacks, h2_session);
	nghttp2_session_callbacks_del(callbacks);
	nghttp2_option_del(opts);

	nghttp2_submit_settings(
		h2_session->ng_session, NGHTTP2_FLAG_NONE, NULL, 0);
	nghttp2_session_send(h2_session->ng_session);

	bufferevent_setcb(
		h2_session->bev, bev_read_cb, NULL,
		bev_event_cb, h2_session);
	bufferevent_enable(h2_session->bev, EV_READ | EV_WRITE);
}

h2c_server_t *h2c_server_init(
	const char *bind_addr, uint16_t port,
	h2c_request_cb req_cb, void *user_data)
{
	h2c_server_t *server = calloc(1, sizeof(h2c_server_t));
	server->base = event_base_new();
	server->req_cb = req_cb;
	server->user_data = user_data;

	struct sockaddr_in sin = {0};
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	inet_pton(AF_INET, bind_addr, &sin.sin_addr);

	server->listener = evconnlistener_new_bind(
		server->base, accept_cb, server,
		LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, -1,
		(struct sockaddr *)&sin, sizeof(sin));
	return server;
}

h2c_server_t *h2c_server_init_uds(
	const char *uds_path,
	h2c_request_cb req_cb, void *user_data)
{
	h2c_server_t *server = calloc(1, sizeof(h2c_server_t));
	server->base = event_base_new();
	server->req_cb = req_cb;
	server->user_data = user_data;

	/* Remove stale socket file if it still exists */
	unlink(uds_path);

	struct sockaddr_un sun = {0};
	sun.sun_family = AF_UNIX;
	strncpy(sun.sun_path, uds_path, sizeof(sun.sun_path) - 1);

	server->listener = evconnlistener_new_bind(
		server->base, accept_cb, server,
		LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, -1,
		(struct sockaddr *)&sun, sizeof(sun));
	return server;
}

void h2c_server_run(h2c_server_t *server) {
	event_base_dispatch(server->base);
}

void h2c_server_destroy(h2c_server_t *server) {
	if (!server) return;
	if (server->listener)
		evconnlistener_free(server->listener);
	if (server->base)
		event_base_free(server->base);
	free(server);
}

int h2c_send_response_ex(
	h2c_session_t *session, int32_t stream_id,
	int status_code, const char *content_type,
	const char *location,
	const char *extra_hdr_name,
	const char *extra_hdr_value,
	const uint8_t *body, size_t body_len)
{
	char status_str[4];
	snprintf(status_str, sizeof(status_str), "%d", status_code);

	/* Max headers: :status + content-type + location + extra = 4 */
	nghttp2_nv hdrs[4];
	int hdr_count = 0;

	/* :status */
	hdrs[hdr_count].name = (uint8_t *)":status";
	hdrs[hdr_count].namelen = 7;
	hdrs[hdr_count].value = (uint8_t *)status_str;
	hdrs[hdr_count].valuelen = strlen(status_str);
	hdrs[hdr_count].flags = NGHTTP2_NV_FLAG_NONE;
	hdr_count++;

	/* content-type */
	if (content_type) {
		hdrs[hdr_count].name = (uint8_t *)"content-type";
		hdrs[hdr_count].namelen = 12;
		hdrs[hdr_count].value = (uint8_t *)content_type;
		hdrs[hdr_count].valuelen = strlen(content_type);
		hdrs[hdr_count].flags = NGHTTP2_NV_FLAG_NONE;
		hdr_count++;
	}

	/* location */
	if (location) {
		hdrs[hdr_count].name = (uint8_t *)"location";
		hdrs[hdr_count].namelen = 8;
		hdrs[hdr_count].value = (uint8_t *)location;
		hdrs[hdr_count].valuelen = strlen(location);
		hdrs[hdr_count].flags = NGHTTP2_NV_FLAG_NONE;
		hdr_count++;
	}

	/* extra header (e.g. Allow) */
	if (extra_hdr_name && extra_hdr_value) {
		hdrs[hdr_count].name = (uint8_t *)extra_hdr_name;
		hdrs[hdr_count].namelen = strlen(extra_hdr_name);
		hdrs[hdr_count].value = (uint8_t *)extra_hdr_value;
		hdrs[hdr_count].valuelen = strlen(extra_hdr_value);
		hdrs[hdr_count].flags = NGHTTP2_NV_FLAG_NONE;
		hdr_count++;
	}

	nghttp2_data_provider data_prd = {0};
	data_source_t *src = NULL;
	int has_body = (body && body_len > 0) ? 1 : 0;

	if (has_body) {
		src = malloc(sizeof(data_source_t));
		/* Copy the body so it survives after the caller */
		/* returns (who may free(body) immediately after). */
		src->data = malloc(body_len);
		if (src->data) {
			memcpy((void *)src->data, body, body_len);
		}
		src->len = body_len;
		src->offset = 0;
		data_prd.source.ptr = src;
		data_prd.read_callback = data_read_callback;
	}

	nghttp2_submit_response(
		session->ng_session, stream_id, hdrs, hdr_count,
		has_body ? &data_prd : NULL);
	nghttp2_session_send(session->ng_session);

	/* NOTE: Do not free src->data and src here!
	 * nghttp2 may call data_read_callback multiple times
	 * to send the body in chunks. The data must remain
	 * valid until nghttp2 finishes sending it. For now,
	 * we leave it in memory (minor leak for small
	 * responses). TODO: Implement a mechanism to free
	 * the data when the stream is closed. */
	return 0;
}

int h2c_send_response(
	h2c_session_t *session, int32_t stream_id,
	int status_code, const char *content_type,
	const char *location,
	const uint8_t *body, size_t body_len)
{
	return h2c_send_response_ex(
		session, stream_id, status_code, content_type,
		location, NULL, NULL, body, body_len);
}

/**
 * @brief Read callback for SSE streams.
 * Renvoie NGHTTP2_ERR_DEFERRED quand la file est vide,
 * permettant de pousser des données de manière asynchrone.
 */
static ssize_t sse_data_read_callback(
	nghttp2_session *session UNUSED,
	int32_t stream_id UNUSED,
	uint8_t *buf, size_t length,
	uint32_t *data_flags UNUSED,
	nghttp2_data_source *source,
	void *user_data UNUSED)
{
	h2c_sse_stream_t *stream = (h2c_sse_stream_t *)source->ptr;

	if (!stream->head) {
		/* File vide : différer jusqu'à ce que des données arrivent */
		stream->deferred = true;
		return NGHTTP2_ERR_DEFERRED;
	}

	/* Récupérer le premier item de la file */
	sse_queue_item_t *item = stream->head;
	size_t to_copy = item->len < length ? item->len : length;

	memcpy(buf, item->data, to_copy);

	/* Si tout a été copié, retirer l'item de la file */
	if (to_copy == item->len) {
		stream->head = item->next;
		if (!stream->head) {
			stream->tail = NULL;
		}
		free(item->data);
		free(item);
	} else {
		/* Données partielles : ajuster l'item */
		size_t remaining = item->len - to_copy;
		uint8_t *new_data = malloc(remaining);
		if (new_data) {
			memcpy(new_data, item->data + to_copy, remaining);
			free(item->data);
			item->data = new_data;
			item->len = remaining;
		}
	}

	/* Ne jamais mettre EOF pour un flux SSE (stream infini) */
	return (ssize_t)to_copy;
}

h2c_sse_stream_t *h2c_sse_stream_open(
	h2c_session_t *session, int32_t stream_id)
{
	h2c_sse_stream_t *stream = calloc(1, sizeof(h2c_sse_stream_t));
	if (!stream) return NULL;

	stream->session = session;
	stream->stream_id = stream_id;
	stream->deferred = false;

	/* Headers SSE */
	nghttp2_nv hdrs[] = {
		MAKE_NV(":status", "200", 3),
		MAKE_NV("content-type", "text/event-stream", 17),
		MAKE_NV("cache-control", "no-cache", 8)
	};

	/* Data provider pour le flux SSE */
	nghttp2_data_provider data_prd = {0};
	data_prd.source.ptr = stream;
	data_prd.read_callback = sse_data_read_callback;

	/* Soumettre la réponse avec headers + data provider */
	nghttp2_submit_response(
		session->ng_session, stream_id,
		hdrs, 3, &data_prd);
	nghttp2_session_send(session->ng_session);

	return stream;
}

int h2c_sse_stream_push(
	h2c_sse_stream_t *stream,
	const uint8_t *data, size_t data_len)
{
	if (!stream || !data || data_len == 0) return -1;

	/* Allouer un nouvel item pour la file */
	sse_queue_item_t *item = malloc(sizeof(sse_queue_item_t));
	if (!item) return -1;

	item->data = malloc(data_len);
	if (!item->data) {
		free(item);
		return -1;
	}
	memcpy(item->data, data, data_len);
	item->len = data_len;
	item->next = NULL;

	/* Ajouter à la file */
	if (stream->tail) {
		stream->tail->next = item;
	} else {
		stream->head = item;
	}
	stream->tail = item;

	/* Si le callback était en attente, le réveiller */
	if (stream->deferred) {
		stream->deferred = false;
		nghttp2_session_resume_data(
			stream->session->ng_session,
			stream->stream_id);
		nghttp2_session_send(stream->session->ng_session);
	}

	return 0;
}

void h2c_sse_stream_close(h2c_sse_stream_t *stream)
{
	if (!stream) return;

	/* Vider la file d'attente */
	while (stream->head) {
		sse_queue_item_t *item = stream->head;
		stream->head = item->next;
		free(item->data);
		free(item);
	}

	/* Terminer le stream HTTP/2 */
	nghttp2_submit_rst_stream(
		stream->session->ng_session,
		NGHTTP2_FLAG_NONE,
		stream->stream_id,
		NGHTTP2_NO_ERROR);
	nghttp2_session_send(stream->session->ng_session);

	free(stream);
}

nghttp2_session *h2c_session_get_nghttp2(h2c_session_t *session)
{
	if (!session) return NULL;
	return session->ng_session;
}

const char *h2c_session_get_content_type(
	h2c_session_t *session)
{
	if (!session) return NULL;
	return session->content_type[0] ?
	       session->content_type : NULL;
}

const char *h2c_session_get_accept(
	h2c_session_t *session)
{
	if (!session) return NULL;
	return session->accept[0] ?
	       session->accept : NULL;
}

const char *h2c_session_get_method(h2c_session_t *session)
{
	if (!session) return NULL;
	return session->method[0] ?
	       session->method : NULL;
}

struct event_base *h2c_server_get_event_base(h2c_server_t *server)
{
	if (!server) return NULL;
	return server->base;
}