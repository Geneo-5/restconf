/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#include "restconf/h2c.h"
#include <stroll/falloc.h>
#include <stroll/page.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <nghttp2/nghttp2.h>

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

struct rest_stream {
	struct stroll_dlist_node  node;
	struct rest_session      *parent;
};

static void
destroy_stream(struct rest_stream *stream)
{
	stroll_dlist_remove(&stream->node);
	stroll_falloc_free(&stream->parent->parent->stream_alloc, stream);
}

static void
destroy_session(struct rest_session *sess)
{
	struct rest_stream *stream;
	struct rest_stream *tmp;

	if (!sess)
		return;

	stroll_dlist_remove(&sess->node);
	nghttp2_session_del(sess->ng_session);
	stroll_dlist_foreach_entry_safe(&sess->streams, stream, node, tmp) {
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
	destroy_session(sess);
}

static void
bev_event(struct bufferevent *bev __unused, short events, void *ctx)
{
	struct rest_session *sess = ctx;

	if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT))
		destroy_session(sess);

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

	nghttp2_session_callbacks_new(&callbacks);
	//nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks,
	//	on_begin_headers);
	//nghttp2_session_callbacks_set_on_stream_close_callback(callbacks,
	//	on_stream_close);

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
	sess->bev = bufferevent_socket_new(srv->base, fd, BEV_OPT_CLOSE_ON_FREE);
	if (!sess->bev)
		goto error;

	if (srv->idle_timeout_sec > 0) {
		struct timeval tv = { srv->idle_timeout_sec, 0 };
		bufferevent_set_timeouts(sess->bev, &tv, NULL);
	}

	bufferevent_setcb(sess->bev, bev_read, NULL, bev_event, sess);
	bufferevent_enable(sess->bev, EV_READ | EV_WRITE);
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
	stroll_falloc_init_block_size(&server->stream_alloc,
	                              STROLL_FALLOC_UNBOUND_CHUNK_NR,
	                              sizeof(struct rest_stream),
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
	if (!server)
		return;

	evconnlistener_free(server->listener);
	free(server);
}

