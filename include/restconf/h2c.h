/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#ifndef _RESTCONF_H2C_H
#define _RESTCONF_H2C_H

/**
 * @file
 * @brief HTTP/2 cleartext (h2c) RESTCONF server core.
 *
 * This is the core of the RESTCONF backend: an HTTP/2-over-cleartext (h2c)
 * server built on top of libevent (connection/event I/O) and libnghttp2
 * (HTTP/2 framing). It never terminates TLS and never speaks HTTP/1.1 by
 * itself ; it is meant to be deployed behind a TLS-terminating reverse
 * proxy speaking h2c "prior knowledge" to the backend.
 *
 * The module exposes:
 *  - a server/session/stream object model (#rest_server, and the opaque
 *    #rest_stream handed to resource handlers) ;
 *  - a resource dispatch mechanism: handlers for a given URI path prefix
 *    are registered at build time via #ADD_DISPATCHER, implementing the
 *    #rest_ops callback set, and looked up per incoming request ;
 *  - a few helpers to emit HTTP/2 responses (h2c_send_error(),
 *    h2c_send_options(), h2c_send_answer()).
 */

#include "restconf/cdef.h"
#include <sysrepo.h>
#include <event2/event.h>
#include <sys/un.h>
#include <nghttp2/nghttp2.h>
#include <event2/buffer.h>
#include <curl/curl.h>

#define WWW_AUTH     "WWW-Authenticate"
#define WWW_AUTH_401 "Bearer realm=\"restconf\""
#define WWW_AUTH_403 "Bearer realm=\"restconf\",error=\"insufficient_scope\""
#define CONTENT_JSON "application/yang-data+json"
#define CONTENT_XML  "application/yang-data+XML"


/**
 * @def MAKE_NV
 * @brief Build an nghttp2 name/value header pair literal.
 *
 * @param NAME     header field name, as a C string literal (its length is
 *                 derived at compile time via @c sizeof).
 * @param VALUE    header field value buffer (may be @c NULL if @p VALUELEN
 *                 is @c 0, e.g. to be filled in later).
 * @param VALUELEN length, in bytes, of @p VALUE.
 *
 * Expands to an ::nghttp2_nv compound literal with
 * ::NGHTTP2_NV_FLAG_NONE flags, suitable for use in the @c hdrs array
 * passed to h2c_send_answer() or nghttp2 submission functions.
 */
#define MAKE_NV(NAME, VALUE, VALUELEN) \
	((nghttp2_nv){(uint8_t *)(NAME), (uint8_t *)(VALUE), \
				  sizeof(NAME) - 1, VALUELEN, \
				  NGHTTP2_NV_FLAG_NONE})

/**
 * @def MAKE_NV_OK
 * @brief Shorthand ::nghttp2_nv header pair for a @c ":status: 200" header.
 *
 * Built via #MAKE_NV. Typically used as the first entry of the @c hdrs
 * array passed to h2c_send_answer() when signaling a successful response.
 */
#define MAKE_NV_OK MAKE_NV(":status", "200", 3)

#define MAKE_NV_TEMP(NAME) MAKE_NV(NAME, NULL, 0)

static inline void __rest_nonull(1, 2)
set_nv(nghttp2_nv *nv, char *value)
{
	nv->value = (uint8_t *)value;
	nv->valuelen = strlen(value);
}

static inline void __rest_nonull(1, 2)
make_nv(nghttp2_nv *nv, char *name, char *value)
{
	nv->name = (uint8_t *)name;
	nv->namelen = strlen(name);
	nv->value = (uint8_t *)value;
	nv->valuelen = strlen(value);
	nv->flags = NGHTTP2_NV_FLAG_NONE;
}

/**
 * @brief HTTP request method, as parsed from the @c :method pseudo-header.
 */
enum rest_method {
	METHOD_UNKNOW,   /**< Unrecognized/unsupported HTTP method. */
	METHOD_OPTIONS,  /**< @c OPTIONS */
	METHOD_HEAD,     /**< @c HEAD */
	METHOD_GET,      /**< @c GET */
	METHOD_POST,     /**< @c POST */
	METHOD_PUT,      /**< @c PUT */
	METHOD_PATCH,    /**< @c PATCH */
	METHOD_DELETE,   /**< @c DELETE */
};

/**
 * @brief Opaque h2c server context.
 *
 * Owns the connection listener, the pool allocators used for sessions and
 * streams, and the list of currently active sessions. Created by
 * create_server() (or the create_tcp_server() / create_uds_server()
 * wrappers) and destroyed by destroy_server().
 */
struct rest_server;

/**
 * @brief Opaque per-HTTP/2-stream request context.
 *
 * Represents a single in-flight HTTP/2 request/response exchange. Handed to
 * resource handler callbacks (see #rest_ops) throughout the lifetime of the
 * request, and used as the target argument of the h2c_send_error(),
 * h2c_send_options() and h2c_send_answer() response helpers.
 */
struct rest_stream;

/**
 * @brief Resource handler callback set.
 *
 * Implemented by each RESTCONF resource handler and registered for a given
 * URI path prefix via #ADD_DISPATCHER. The handler's private,
 * implementation-defined per-stream state is stored inline in the
 * #rest_stream::priv flexible array member (sized via the @p priv_len
 * argument of #ADD_DISPATCHER) and passed as the opaque @p priv pointer to
 * every callback below.
 *
 * Typical callback sequence for a single request: #init once the request's
 * @c :path has matched this handler's prefix, then #header once per
 * remaining request header, then #dispatch once the request is fully
 * received (headers and, if any, body), and finally #fini when the stream
 * is torn down.
 */
struct rest_ops {
	/**
	 * @brief Called once a request path has matched this handler.
	 *
	 * @param priv   opaque per-stream private state (see #rest_ops).
	 * @param stream owning stream, to be used for any subsequent
	 *               h2c_send_error() / h2c_send_options() /
	 *               h2c_send_answer() call related to this request.
	 * @param method HTTP method parsed from the @c :method pseudo-header.
	 * @param url    parsed request URI (see libcurl's @c CURLU), still
	 *               owned by the caller.
	 *
	 * @return @c 0 on success, or a negative @c NGHTTP2_ERR_* code to
	 *         abort processing of this stream.
	 */
	int (*init)(void *priv, struct rest_stream *stream, enum rest_method method, CURLU *url);
	/**
	 * @brief Called once for every remaining HTTP/2 request header.
	 *
	 * @param priv  opaque per-stream private state (see #rest_ops).
	 * @param name  header field name (raw, non-owning buffer).
	 * @param value header field value (raw, non-owning buffer).
	 *
	 * @return @c 0 on success, or a negative @c NGHTTP2_ERR_* code to
	 *         abort processing of this stream.
	 */
	int (*header)(void *priv, nghttp2_rcbuf *name, nghttp2_rcbuf *value);
	/**
	 * @brief Called once the request (headers and body) is fully received.
	 *
	 * Expected to actually process the request and produce a response,
	 * typically via h2c_send_error(), h2c_send_options() or
	 * h2c_send_answer().
	 *
	 * @param priv opaque per-stream private state (see #rest_ops).
	 * @param body accumulated request body, if any (may be empty).
	 *
	 * @return @c 0 on success, or a negative @c NGHTTP2_ERR_* code to
	 *         abort processing of this stream.
	 */
	int (*dispatch)(void *priv, struct evbuffer *body);

	int (*identified)(void *priv, const char *name);
	/**
	 * @brief Called when the stream is being torn down.
	 *
	 * Meant to release any resource allocated by #init / #header /
	 * #dispatch and referenced from @p priv.
	 *
	 * @param priv opaque per-stream private state (see #rest_ops).
	 */
	void (*fini)(void *priv);
};

/**
 * @brief Static registration entry associating a URI path prefix with a
 *        #rest_ops resource handler.
 *
 * Instances are built by #ADD_DISPATCHER and placed in the @c
 * rest_dispatcher linker section ; the resulting table, delimited by
 * #__start_rest_dispatcher and #__stop_rest_dispatcher, is scanned at
 * runtime to route each incoming request's @c :path to the appropriate
 * handler.
 */
struct rest_dispatcher {
	const char            *path;     /**< URI path prefix to match (e.g. @c "/.well-known/"). */
	size_t                 path_len; /**< Length of #path, in bytes. */
	const struct rest_ops *ops;      /**< Handler callback set for this prefix. */
	size_t                 priv_len; /**< Size, in bytes, of the handler's private per-stream state. */
};

int
jwt_bearer_token_get_name(const char *bearer, char **user)
	__rest_nonull(1, 2);


sr_conn_ctx_t *
h2c_stream_get_conn(struct rest_stream *stream)
	__rest_nonull(1);

/**
 * @def ADD_DISPATCHER
 * @brief Register a resource handler for a given URI path prefix.
 *
 * @param _str     string literal URI path prefix (e.g. @c "/.well-known/").
 * @param opsl     lvalue of type <tt>const struct rest_ops</tt> holding the
 *                 handler's callback set.
 * @param _priv_len size, in bytes, of the per-stream private state the
 *                  handler needs ; reserved inline in every #rest_stream
 *                  routed to this handler.
 *
 * Defines a static #rest_dispatcher entry placed in the @c rest_dispatcher
 * linker section, so it is automatically picked up by the routing lookup
 * performed for every incoming request, with no need for explicit manual
 * registration elsewhere.
 */
#define ADD_DISPATCHER(_str, opsl, _priv_len) \
static const struct rest_dispatcher __rest_dispatcher_##opsl = { \
	.path     = _str, \
	.path_len = sizeof(_str) - 1, \
	.ops      = &(opsl), \
	.priv_len = _priv_len}; \
static const struct rest_dispatcher * ___rest_dispatcher_##opsl \
__attribute((section("rest_dispatcher"), used)) = &__rest_dispatcher_##opsl;

/**
 * @brief Start-of-table marker for the @c rest_dispatcher linker section.
 * @see ADD_DISPATCHER, __stop_rest_dispatcher
 */
extern const struct rest_dispatcher *__start_rest_dispatcher[];
/**
 * @brief End-of-table marker for the @c rest_dispatcher linker section.
 * @see ADD_DISPATCHER, __start_rest_dispatcher
 */
extern const struct rest_dispatcher *__stop_rest_dispatcher[];

/**
 * @brief Send a bare HTTP error response and terminate the stream.
 *
 * Emits a response made of a single @c :status header (and, for @c 401 /
 * @c 403, the corresponding @c WWW-Authenticate header), with no body.
 * Marks @p stream as done.
 *
 * @param stream target stream to answer on. Must not be @c NULL.
 * @param error  HTTP status code to send (e.g. @c 404, @c 405, @c 406,
 *               @c 401, @c 403). Must be strictly less than @c 999.
 *
 * @return @c 0 on success, or a negative @c NGHTTP2_ERR_* code on failure.
 */
int
h2c_send_error(struct rest_stream  *stream, uint16_t error)
	__rest_nonull(1);

/**
 * @brief Send an @c OPTIONS response advertising the allowed methods.
 *
 * Emits a @c 200 response carrying an @c Allow header set to @p options,
 * with no body. Marks @p stream as done.
 *
 * @param stream  target stream to answer on. Must not be @c NULL.
 * @param options comma-separated list of allowed HTTP methods (e.g.
 *                @c "GET, HEAD, OPTIONS"), used as the @c Allow header
 *                value. Must not be @c NULL.
 *
 * @return @c 0 on success, or a negative @c NGHTTP2_ERR_* code on failure.
 */
int
h2c_send_options(struct rest_stream  *stream, char *options)
	__rest_nonull(1);

/**
 * @brief Send a full HTTP response, with headers and an optional body.
 *
 * Submits an HTTP/2 response made of the given header set and, if @p out is
 * non-@c NULL, streams its content back to the client as the response
 * body (the buffer is drained as it is sent).
 *
 * @param stream target stream to answer on. Must not be @c NULL.
 * @param hdrs   array of response headers (including the @c :status
 *               pseudo-header, e.g. built via #MAKE_NV / #MAKE_NV_OK).
 *               Must not be @c NULL.
 * @param nb     number of entries in @p hdrs.
 * @param out    response body buffer, or @c NULL for a bodyless response
 *               (e.g. @c HEAD requests or @c 204 responses).
 *
 * @return @c 0 on success, or a negative @c NGHTTP2_ERR_* code on failure.
 */
int
h2c_send_answer(struct rest_stream  *stream, nghttp2_nv *hdrs, size_t nb, struct evbuffer *out)
	__rest_nonull(1, 2);


void
h2c_discard_body(struct rest_stream  *stream)
	__rest_nonull(1);

/**
 * @brief Create and bind an h2c server on an arbitrary socket address.
 *
 * Allocates the server context, its session/stream pool allocators, and
 * starts listening for incoming connections on @p sock via libevent.
 * Prefer the create_tcp_server() and create_uds_server() convenience
 * wrappers over calling this function directly.
 *
 * @param base libevent event base the server's listener and sessions are
 *             attached to. Must not be @c NULL.
 * @param conn sysrepo connection handed to resource handlers as needed.
 *             Must not be @c NULL.
 * @param sock socket address to bind and listen on. Must not be @c NULL.
 * @param len  length, in bytes, of @p sock.
 *
 * @return a newly allocated #rest_server on success, or @c NULL on failure
 *         (with @c errno set accordingly).
 *
 * @see create_tcp_server(), create_uds_server(), destroy_server()
 */
struct rest_server *
create_server(struct event_base  *base,
              sr_conn_ctx_t      *conn,
              struct sockaddr    *sock,
              int                 len)
	__rest_nonull(1, 2, 3);

/**
 * @brief Destroy an h2c server and all its active sessions/streams.
 *
 * Stops the connection listener, tears down every still-active session (and
 * their streams, invoking each stream's #rest_ops::fini as appropriate),
 * and releases the server context itself.
 *
 * @param server server to destroy, or @c NULL (no-op).
 */
void
destroy_server(struct rest_server *server);

/**
 * @brief Create and bind an h2c server listening on a Unix domain socket.
 *
 * Convenience wrapper around create_server() building the @c AF_UNIX socket
 * address from @p uds_path.
 *
 * @param base     libevent event base. Must not be @c NULL.
 * @param conn     sysrepo connection. Must not be @c NULL.
 * @param uds_path filesystem path of the Unix domain socket to bind and
 *                 listen on. Must not be @c NULL. Truncated to fit
 *                 @c sockaddr_un::sun_path if too long.
 *
 * @return a newly allocated #rest_server on success, or @c NULL on failure
 *         (with @c errno set accordingly).
 *
 * @see create_server(), create_tcp_server()
 */
struct rest_server *
create_uds_server(struct event_base  *base,
                  sr_conn_ctx_t      *conn,
                  const char         *uds_path,
                  gid_t               gid)
	__rest_nonull(1, 2, 3);

/**
 * @brief Create and bind an h2c server listening on a TCP/IPv4 socket.
 *
 * Convenience wrapper around create_server() building the @c AF_INET socket
 * address from @p bind_addr and @p port.
 *
 * @param base      libevent event base. Must not be @c NULL.
 * @param conn      sysrepo connection. Must not be @c NULL.
 * @param bind_addr dotted-decimal IPv4 address to bind on. Must not be
 *                  @c NULL.
 * @param port      TCP port to listen on, in host byte order.
 *
 * @return a newly allocated #rest_server on success, or @c NULL on failure
 *         (with @c errno set accordingly).
 *
 * @see create_server(), create_uds_server()
 */
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

/**
 * @brief Set the per-connection idle read timeout.
 *
 * Applies to connections accepted after this call ; existing connections
 * keep whatever timeout was in effect when they were established.
 *
 * @param server      target server. Must not be @c NULL.
 * @param timeout_sec idle read timeout, in seconds ; @c 0 disables the
 *                    timeout. Must be @c >= 0.
 */
void
server_set_idle_timeout(struct rest_server *server, int timeout_sec)
	__rest_nonull(1);


#endif /* _RESTCONF_H2C_H */
