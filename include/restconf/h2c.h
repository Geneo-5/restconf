/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#ifndef _RESTCONF_H2C_H
#define _RESTCONF_H2C_H

#include "restconf/cdef.h"
#include <sysrepo.h>
#include <event2/event.h>
#include <sys/un.h>
#include <nghttp2/nghttp2.h>
#include <event2/buffer.h>
#include <curl/curl.h>

enum rest_method {
	METHOD_UNKNOW,
	METHOD_OPTIONS,
	METHOD_HEAD,
	METHOD_GET,
	METHOD_POST,
	METHOD_PUT,
	METHOD_PATCH,
	METHOD_DELETE,
};

struct rest_server;
struct rest_stream;

struct rest_ops {
	int (*init)(void *priv, struct rest_stream *stream, enum rest_method method, CURLU *url);
	int (*header)(void *priv, nghttp2_rcbuf *name, nghttp2_rcbuf *value);
	int (*dispatch)(void *priv, struct evbuffer *body);
	void (*fini)(void *priv);
};

struct rest_dispatcher {
	const char            *path;
	size_t                 path_len;
	const struct rest_ops *ops;
	size_t                 priv_len;
};

#define ADD_DISPATCHER(_str, opsl, _priv_len) \
static const struct rest_dispatcher __rest_dispatcher_##opsl \
__attribute((section("rest_dispatcher"), used)) = { \
	.path     = _str, \
	.path_len = sizeof(_str) - 1, \
	.ops      = &(opsl), \
	.priv_len = _priv_len \
};

extern const struct rest_dispatcher __start_rest_dispatcher;
extern const struct rest_dispatcher __stop_rest_dispatcher;

struct rest_server *
create_server(struct event_base  *base,
              sr_conn_ctx_t      *conn,
              struct sockaddr    *sock,
              int                 len)
	__rest_nonull(1, 2, 3);

int
h2c_send_error(struct rest_stream  *stream, uint16_t error)
	__rest_nonull(1);


int
h2c_send_options(struct rest_stream  *stream, char *options)
	__rest_nonull(1);

static inline
struct rest_server * __rest_nonull(1, 2, 3)
create_uds_server(struct event_base  *base,
                  sr_conn_ctx_t      *conn,
                  const char         *uds_path)
{
	struct sockaddr_un sun = {0};
	sun.sun_family = AF_UNIX;
	strncpy(sun.sun_path, uds_path, sizeof(sun.sun_path) - 1);
	return create_server(base, conn, (struct sockaddr *)&sun, sizeof(sun));
}

static inline
struct rest_server * __rest_nonull(1, 2, 3)
create_tcp_server(struct event_base *base,
                  sr_conn_ctx_t     *conn,
                  const char        *bind_addr,
                  uint16_t           port)
{
	struct sockaddr_in sin = {0};
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	inet_pton(AF_INET, bind_addr, &sin.sin_addr);
	return create_server(base, conn, (struct sockaddr *)&sin, sizeof(sin));
}

void
server_set_idle_timeout(struct rest_server *server, int timeout_sec)
	__rest_nonull(1);

void
destroy_server(struct rest_server *server);

#endif /* _RESTCONF_H2C_H */
