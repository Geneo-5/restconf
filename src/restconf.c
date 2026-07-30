/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#include "restconf/h2c.h"
#include <sysrepo.h>
#include <errno.h>

struct restconf_ctx {
	struct rest_stream *stream;
	enum rest_method    method;
	struct evbuffer    *output;
	char               *username;
	sr_datastore_t      datastore;
	sr_session_ctx_t   *session;
	char               *xpath;
	LYD_FORMAT          ly_fmt;
};

static int
parse_path(struct restconf_ctx *ctx, char *path __unused)
{
	ctx->datastore = SR_DS_RUNNING;
	ctx->xpath = strdup("/*");
	return 0;
}

static int
restconf_init(void               *priv,
              struct rest_stream *stream,
              enum rest_method    method,
              CURLU              *url)
{
	struct restconf_ctx *ctx = priv;
	char *path;
	int ret;

	ctx->stream   = stream;
	ctx->method   = method;
	ctx->output   = NULL;
	ctx->session  = NULL;
	ctx->xpath    = NULL;
	ctx->username = NULL;
	ctx->ly_fmt   = LYD_JSON;

	if (curl_url_get(url, CURLUPART_PATH, &path, 0))
		return -1;

	ret = parse_path(ctx, path);
	curl_free(path);
	return ret;
}

static void
restconf_fini(void *priv) {
	struct restconf_ctx *ctx = priv;

	free(ctx->username);
	free(ctx->xpath);
	sr_session_stop(ctx->session);
	if (ctx->output)
		evbuffer_free(ctx->output);

}

static int
restconf_header(void          *priv __unused,
                nghttp2_rcbuf *name __unused,
                nghttp2_rcbuf *value __unused)
{
	// struct restconf_ctx *ctx = priv;
	// nghttp2_vec            vname = nghttp2_rcbuf_get_buf(name);
	// nghttp2_vec            vvalue = nghttp2_rcbuf_get_buf(value);

	return 0;
}

static int
restconf_identified(void *priv, const char *name)
{
	struct restconf_ctx *ctx = priv;

	ctx->username = strdup(name);
	return ctx->username ? 0 : -ENOMEM;
}

static ssize_t
restconf_write_cb(void *priv, const void *buf, size_t count)
{
	struct restconf_ctx *ctx = priv;

	if (evbuffer_add(ctx->output, buf, count))
		return -ENOMEM;

	return (ssize_t)count;
}

static int
restconf_send_answer(struct restconf_ctx *ctx, const struct lyd_node *root)
{
	uint32_t options = LYD_PRINT_SIBLINGS | LYD_PRINT_SHRINK;
	nghttp2_nv hdrs[] = {
		MAKE_NV_OK,
	};

	rest_assert(!ctx->output);
	ctx->output = evbuffer_new();
	if (!ctx->output)
		return -ENOMEM;

	options |= LYD_PRINT_WD_ALL;
	lyd_print_clb(restconf_write_cb, ctx, root, ctx->ly_fmt, options);
	return h2c_send_answer(ctx->stream, hdrs, stroll_array_nr(hdrs), ctx->output);
}

static int
restconf_dispatch(void            *priv __unused,
                  struct evbuffer *body __unused)
{
	struct restconf_ctx *ctx = priv;
	sr_conn_ctx_t       *conn = h2c_stream_get_conn(ctx->stream);
	sr_data_t           *data = NULL;
	int                  ret;

	if (!ctx->username)
		h2c_send_error(ctx->stream, 401);

	if (!ctx->xpath)
		h2c_send_error(ctx->stream, 404);

	sr_session_start(conn, ctx->datastore, &ctx->session);
	sr_session_set_user(ctx->session, ctx->username);


	sr_get_data(ctx->session, ctx->xpath, 0, 0, 0, &data);
	ret = restconf_send_answer(ctx, data->tree);
	sr_release_data(data);
	return ret;


	// return h2c_send_answer(ctx->stream, hdrs, stroll_array_nr(hdrs), NULL);
}

static const struct rest_ops restconf_ops = {
	.init       = restconf_init,
	.header     = restconf_header,
	.identified = restconf_identified,
	.dispatch   = restconf_dispatch,
	.fini       = restconf_fini
};

ADD_DISPATCHER(CONFIG_H2C_RESTCONF_ROOT, restconf_ops, sizeof(struct restconf_ctx))
