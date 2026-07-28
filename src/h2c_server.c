/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#include "restconf/h2c.h"
#include <stroll/falloc.h>
#include <stroll/dlist.h>
#include <stroll/lvstr.h>
#include <stroll/page.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>

struct rest_server {
	struct event_base        *base;
	sr_conn_ctx_t            *conn;
	struct evconnlistener    *listener;
	struct stroll_dlist_node  sessions;
	struct stroll_falloc      sess_alloc;
	struct stroll_falloc      stream_alloc;
	int                       idle_timeout_sec;
};

struct rest_session {
	struct stroll_dlist_node  node;
	struct rest_server       *parent;
	struct bufferevent       *bev;
	nghttp2_session          *ng_session;
	struct stroll_dlist_node  streams;
	int                       fd;
};

enum stream_status {
	STATUS_AUTH_UNKNOW,
	STATUS_AUTH_VALID,
	STATUS_AUTH_INVALID,
	STATUS_DONE,
};

struct rest_stream {
	struct stroll_dlist_node  node;
	struct rest_session      *parent;
	int32_t                   stream_id;
	enum rest_method          method;
	struct evbuffer          *body;
	const struct rest_ops    *ops;
	enum stream_status        status;
	uint8_t                   priv[] __aligned;
};

static enum rest_method
h2c_parse_method(const char *method)
{
	rest_assert(method);

	switch (strlen(method)) {
	case 3:
		if (strcmp(method, "GET") == 0)
			return METHOD_GET;

		if (strcmp(method, "PUT") == 0)
			return METHOD_PUT;

		break;
	case 4:
		if (strcmp(method, "HEAD") == 0)
			return METHOD_HEAD;

		if (strcmp(method, "POST") == 0)
			return METHOD_POST;

		break;
	case 5:
		if (strcmp(method, "PATCH") == 0)
			return METHOD_PATCH;

		break;
	case 6:
		if (strcmp(method, "DELETE") == 0)
			return METHOD_DELETE;

		break;
	case 7:
		if (strcmp(method, "OPTIONS") == 0)
			return METHOD_OPTIONS;

		break;
	}

	return METHOD_UNKNOW;
}

#define WWW_AUTH     "WWW-Authenticate"
#define WWW_AUTH_401 "Bearer realm=\"restconf\""
#define WWW_AUTH_403 "Bearer realm=\"restconf\",error=\"insufficient_scope\""

int
h2c_send_error(struct rest_stream  *stream, uint16_t err)
{
	nghttp2_nv hdrs[2];
	char status[6];
	size_t nb = 0;

	rest_assert(err < 999);

	stream->status = STATUS_DONE;
	snprintf(status, sizeof(status), "%d", err);
	hdrs[nb].name     = (uint8_t *)":status";
	hdrs[nb].namelen  = 7;
	hdrs[nb].value    = (uint8_t *)status;
	hdrs[nb].valuelen = strlen(status);
	hdrs[nb].flags    = NGHTTP2_NV_FLAG_NONE;
	nb++;

	switch (err) {
	case 401:
		hdrs[nb].name     = (uint8_t *)WWW_AUTH;
		hdrs[nb].namelen  = sizeof(WWW_AUTH) - 1;
		hdrs[nb].value    = (uint8_t *)WWW_AUTH_401;
		hdrs[nb].valuelen = sizeof(WWW_AUTH_401) - 1;
		hdrs[nb].flags    = NGHTTP2_NV_FLAG_NONE;
		nb++;
		break;
	case 403:
		hdrs[nb].name     = (uint8_t *)WWW_AUTH;
		hdrs[nb].namelen  = sizeof(WWW_AUTH) - 1;
		hdrs[nb].value    = (uint8_t *)WWW_AUTH_403;
		hdrs[nb].valuelen = sizeof(WWW_AUTH_403) - 1;
		hdrs[nb].flags    = NGHTTP2_NV_FLAG_NONE;
		nb++;
		break;
	case 404:
	case 405:
	case 406:
		break;
	default:
		rest_assert(0);
	}

	return  nghttp2_submit_response2(stream->parent->ng_session, stream->stream_id,
	                                 hdrs, nb, NULL);
}

int
h2c_send_options(struct rest_stream  *stream, char *options)
{
	nghttp2_nv hdrs[2] = {
		MAKE_NV_OK,
		MAKE_NV("allow", NULL, 0),
	};

	rest_assert(stream);
	rest_assert(options);

	stream->status = STATUS_DONE;
	hdrs[1].value = (uint8_t *)options;
	hdrs[1].valuelen = strlen(options);

	return nghttp2_submit_response2(stream->parent->ng_session, stream->stream_id,
	                                 hdrs, 2, NULL);
}

static ssize_t
read_evbuffer(nghttp2_session     *session __unused,
              int32_t              stream_id __unused,
              uint8_t             *buf,
              size_t               length,
              uint32_t            *data_flags,
              nghttp2_data_source *source,
              void                *ctx __unused)
{
	struct evbuffer *out = source->ptr;
	ssize_t len;

	len = evbuffer_remove(out, buf, length);
	if (!evbuffer_get_length(out))
		*data_flags |= NGHTTP2_DATA_FLAG_EOF;

	return len < 0 ? NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE : len;
}

int
h2c_send_answer(struct rest_stream  *stream,
                nghttp2_nv          *hdrs,
                size_t               nb,
                struct evbuffer     *out)
{
	nghttp2_data_provider2 data_prd = {
		.source.ptr = out,
		.read_callback = read_evbuffer
	};
	return nghttp2_submit_response2(stream->parent->ng_session, stream->stream_id,
	                                 hdrs, nb, out ? &data_prd : NULL);
}

static int
on_frame_recv(nghttp2_session     *session,
              const nghttp2_frame *frame,
              void                *ctx __unused)
{
	struct rest_stream  *stream;
	bool end_stream = (frame->hd.type == NGHTTP2_HEADERS ||
	                   frame->hd.type == NGHTTP2_DATA) &&
	                  (frame->hd.flags & NGHTTP2_FLAG_END_STREAM);
	uint16_t err;

	if (!end_stream)
		return 0;

	stream = nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
	if (!stream || stream->status == STATUS_DONE)
		return 0;

	if (stream->ops) {
		rest_assert(stream->ops->dispatch);
		return stream->ops->dispatch(stream->priv, stream->body);
	}

	switch (stream->status) {
	default:
		rest_assert(0);
	case STATUS_AUTH_UNKNOW:
	case STATUS_AUTH_INVALID:
		err = 401;
		break;
	case STATUS_AUTH_VALID:
		err = 404;
		break;
	}

	return h2c_send_error(stream, err);
}

static int
on_data_chunk_recv(nghttp2_session *session,
                   uint8_t          flags __unused,
                   int32_t          stream_id,
                   const uint8_t   *data,
                   size_t           len,
                   void            *ctx __unused)
{
	struct rest_stream  *stream;
	int ret;

	stream = nghttp2_session_get_stream_user_data(session, stream_id);
	if (!stream || stream->status != STATUS_AUTH_VALID)
		return 0;

	ret = evbuffer_add(stream->body, data, len);
	return ret ? NGHTTP2_ERR_NOMEM : 0;
}

static const struct rest_ops *
search_dispatcher(CURLU *url)
{
	const struct rest_dispatcher * ptr;
	const struct rest_ops *ops = NULL;
	CURLUcode rc;
	char *path;

	rc = curl_url_get(url, CURLUPART_PATH, &path, 0);
	if (rc)
		return NULL;

	for (ptr = &__start_rest_dispatcher; ptr < &__stop_rest_dispatcher; ++ptr) {
		rest_assert(ptr->path);
		rest_assert(ptr->path[0] = '/');

		if (strncmp(ptr->path, path, ptr->path_len) == 0) {
			ops = ptr->ops;
			break;
		}
	}

	curl_free(path);
	return ops;
}

static int
on_header(nghttp2_session     *session __unused,
          const nghttp2_frame *frame,
          nghttp2_rcbuf       *name,
          nghttp2_rcbuf       *value,
          uint8_t              flags __unused,
          void                *ctx __unused)
{
	struct rest_stream  *stream;
	nghttp2_vec          vname = nghttp2_rcbuf_get_buf(name);
	nghttp2_vec          vvalue = nghttp2_rcbuf_get_buf(value);

	if (frame->hd.type != NGHTTP2_HEADERS ||
	    frame->headers.cat != NGHTTP2_HCAT_REQUEST)
		return 0;

	stream = nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
	if (!stream || stream->status == STATUS_DONE)
		return 0;


	switch (vname.len) {
	case 7:
		if (memcmp(vname.base, ":method", 7) == 0) {
			stream->method = h2c_parse_method((const char *)vvalue.base);
			if (stream->method == METHOD_UNKNOW)
				return h2c_send_error(stream, 501);
			return 0;
		}
		break;
	case 5:
		if (memcmp(vname.base, ":path", 5) == 0) {
			CURLUcode rc;
			CURLU *url = curl_url();
			unsigned int cflags = CURLU_DEFAULT_SCHEME |
			                      CURLU_NO_AUTHORITY |
			                      CURLU_DISALLOW_USER;

			rc = curl_url_set(url, CURLUPART_URL, (const char *)vvalue.base, cflags);
			if (rc) {
				printf("URL error: %s -> %s\n", vvalue.base, curl_url_strerror(rc));
				curl_url_cleanup(url);
				return NGHTTP2_ERR_HTTP_HEADER;
			}

			stream->ops = search_dispatcher(url);
			if (stream->ops && stream->ops->init)
				stream->ops->init(stream->priv, stream, stream->method, url);

			curl_url_cleanup(url);
			return 0;
		}
		break;
	}

	if (stream->ops && stream->ops->header)
		return stream->ops->header(stream->priv, name, value);

	return 0;
}

static ssize_t
send_cb(nghttp2_session *session __unused,
        const uint8_t   *data,
        size_t           length,
        int              flags __unused,
        void            *ctx)
{
	struct rest_session *sess = ctx;

	bufferevent_write(sess->bev, data, length);
	return (ssize_t)length;
}

static void
destroy_stream(struct rest_stream *stream)
{
	if (stream->ops && stream->ops->fini)
		stream->ops->fini(stream->priv);

	evbuffer_free(stream->body);
	stroll_falloc_free(&stream->parent->parent->stream_alloc, stream);
}

static struct rest_stream *
create_stream(struct rest_session *sess, int32_t stream_id)
{
	struct rest_stream *stream;

	stream = stroll_falloc_alloc(&sess->parent->stream_alloc);
	if (!stream)
		return NULL;

	stream->body = evbuffer_new();
	if (!stream->body) {
		stroll_falloc_free(&sess->parent->stream_alloc, stream);
		return NULL;
	}

	stream->parent = sess;
	stream->stream_id = stream_id;
	stream->ops = NULL;
	stream->method = METHOD_UNKNOW;
	stream->status = STATUS_AUTH_UNKNOW;
	stroll_dlist_init(&stream->node);
	return stream;
}

static int
on_begin_headers(nghttp2_session     *session,
                 const nghttp2_frame *frame,
                 void                *ctx)
{
	struct rest_session *sess = ctx;
	struct rest_stream  *stream;

	if (frame->hd.type != NGHTTP2_HEADERS ||
	    frame->headers.cat != NGHTTP2_HCAT_REQUEST)
		return 0;

	stream = create_stream(sess, frame->hd.stream_id);
	if (!stream)
		return NGHTTP2_ERR_NOMEM;
	nghttp2_session_set_stream_user_data(session, frame->hd.stream_id, stream);
	stroll_dlist_nqueue_front(&sess->streams, &stream->node);
	return 0;
}

static int
on_stream_close(nghttp2_session *session,
                int32_t          stream_id,
                uint32_t         error_code __unused,
                void            *ctx __unused)
{
	struct rest_stream  *stream;

	stream = nghttp2_session_get_stream_user_data(session, stream_id);
	if (!stream)
		return 0;

	stroll_dlist_remove(&stream->node);
	destroy_stream(stream);
	return 0;
}

static void
destroy_session(struct rest_session *sess)
{
	struct rest_stream *stream;

	if (!sess)
		return;

	nghttp2_session_del(sess->ng_session);
	while (!stroll_dlist_empty(&sess->streams)) {
		stream = stroll_dlist_next_entry((struct rest_stream *)&sess->streams, node);
		stroll_dlist_remove(&stream->node);
		destroy_stream(stream);
	}

	bufferevent_free(sess->bev);
	stroll_falloc_free(&sess->parent->sess_alloc, sess);
}

static void
bev_read(struct bufferevent *bev, void *ctx)
{
	struct rest_session *sess = ctx;
	struct evbuffer *input = bufferevent_get_input(bev);
	size_t           datalen = evbuffer_get_length(input);
	unsigned char   *data = evbuffer_pullup(input, -1);
	nghttp2_ssize    readlen;

	readlen = nghttp2_session_mem_recv2(sess->ng_session, data, datalen);
	if (readlen < 0)
		goto error;

	if (evbuffer_drain(input, (size_t)readlen))
		goto error;

	if (nghttp2_session_send(sess->ng_session))
		goto error;

	return;
error:
	bufferevent_trigger_event(bev, BEV_EVENT_ERROR | BEV_EVENT_READING, 0);
}

static void
bev_event(struct bufferevent *bev __unused, short events, void *ctx)
{
	struct rest_session *sess = ctx;

	if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT)) {
		stroll_dlist_remove(&sess->node);
		destroy_session(sess);
	}

}

static const nghttp2_settings_entry settings[] = {
	{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, CONFIG_H2C_MAX_CONCURRENT_STREAMS},
	{NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, CONFIG_H2C_INITIAL_WINDOW_SIZE},
};

static struct rest_session *
create_session(struct rest_server *srv, evutil_socket_t fd)
{
	struct rest_session       *sess;
	nghttp2_session_callbacks *callbacks;
	nghttp2_option            *opts;

	sess = stroll_falloc_alloc(&srv->sess_alloc);
	if (!sess)
		return NULL;

	sess->parent = srv;
	stroll_dlist_init(&sess->node);
	stroll_dlist_init(&sess->streams);
	sess->fd = fd;
	sess->ng_session = NULL;

	sess->bev = bufferevent_socket_new(srv->base, fd,
		BEV_OPT_CLOSE_ON_FREE);
	if (!sess->bev)
		goto error;

	if (srv->idle_timeout_sec > 0) {
		struct timeval tv = { srv->idle_timeout_sec, 0 };
		bufferevent_set_timeouts(sess->bev, &tv, NULL);
	}

	bufferevent_setcb(sess->bev, bev_read, NULL, bev_event, sess);
	bufferevent_enable(sess->bev, EV_READ | EV_WRITE);
	nghttp2_session_callbacks_new(&callbacks);
	nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks,
		on_begin_headers);
	nghttp2_session_callbacks_set_on_stream_close_callback(callbacks,
		on_stream_close);
	nghttp2_session_callbacks_set_on_header_callback2(callbacks,
		on_header);
	nghttp2_session_callbacks_set_send_callback(
		callbacks, send_cb);
	nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
		callbacks, on_data_chunk_recv);
	nghttp2_session_callbacks_set_on_frame_recv_callback(
		callbacks, on_frame_recv);

	nghttp2_option_new(&opts);
	nghttp2_option_set_no_auto_window_update(opts, 0);

	nghttp2_session_server_new2(&sess->ng_session, callbacks, sess, opts);
	nghttp2_session_callbacks_del(callbacks);
	nghttp2_option_del(opts);
	nghttp2_submit_settings(sess->ng_session,
	                        NGHTTP2_FLAG_NONE,
	                        settings,
	                        stroll_array_nr(settings));
	nghttp2_session_set_local_window_size(sess->ng_session,
	                                      NGHTTP2_FLAG_NONE,
	                                      0,
	                                      CONFIG_H2C_CONNECTION_WINDOW_SIZE);
	nghttp2_session_send(sess->ng_session);
	return sess;
error:
	stroll_falloc_free(&srv->sess_alloc, sess);
	return NULL;
}

static void
accept_cb(struct evconnlistener *listener __unused,
          evutil_socket_t        fd,
          struct sockaddr       *address __unused,
          int                    socklen __unused,
          void                  *ctx)
{
	struct rest_server  *server = ctx;
	struct rest_session *session;
	int                  err;

	err = evutil_make_socket_closeonexec(fd);
	if (err)
		goto close;

	session = create_session(server, fd);
	if (!session)
		goto close;

	stroll_dlist_nqueue_front(&server->sessions, &session->node);
	return;
close:
	evutil_closesocket(fd);
}

struct rest_server *
create_server(struct event_base  *base,
              sr_conn_ctx_t      *conn,
              struct sockaddr    *sock,
              int                 len)
{
	rest_assert(base);
	rest_assert(conn);
	rest_assert(sock);

	struct rest_server *server;
	size_t stream_size = sizeof(struct rest_stream);
	const struct rest_dispatcher * ptr;

	server = malloc(sizeof(*server));
	if (!server) {
		errno = ENOMEM;
		return NULL;
	}

	server->base = base;
	server->conn = conn;
	stroll_dlist_init(&server->sessions);
	stroll_falloc_init_block_size(&server->sess_alloc,
	                              STROLL_FALLOC_UNBOUND_CHUNK_NR,
	                              sizeof(struct rest_session),
	                              stroll_page_size());

	for (ptr = &__start_rest_dispatcher; ptr < &__stop_rest_dispatcher; ++ptr)
		stream_size = stroll_max(stream_size,
		                         sizeof(struct rest_session) + ptr->priv_len);

	stroll_falloc_init_block_size(&server->stream_alloc,
	                              STROLL_FALLOC_UNBOUND_CHUNK_NR,
	                              stream_size,
	                              stroll_page_size());

	server->listener = evconnlistener_new_bind(
		base, accept_cb, server,
		LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE |
		LEV_OPT_CLOSE_ON_EXEC, -1,
		sock, len);
	if (!server->listener)
		goto error;

	return server;
error:
	free(server);
	return NULL;
}

void
server_set_idle_timeout(struct rest_server *server, int timeout_sec)
{
	rest_assert(server);
	rest_assert(timeout_sec >= 0);

	server->idle_timeout_sec = timeout_sec;
}

void
destroy_server(struct rest_server *server)
{
	struct rest_session *sess;

	if (!server)
		return;

	while (!stroll_dlist_empty(&server->sessions)) {
		sess = stroll_dlist_next_entry((struct rest_session *)&server->sessions, node);
		stroll_dlist_remove(&sess->node);
		destroy_session(sess);
	}

	evconnlistener_free(server->listener);
	free(server);
}

