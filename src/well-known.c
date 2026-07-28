/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#include "restconf/h2c.h"

#define ALLOW_OPTIONS "GET, HEAD, OPTIONS"

enum xdr_type {
	XDR_XML,
	XDR_JSON
};

struct well_known_ctx {
	struct rest_stream *stream;
	enum xdr_type       type;
	enum rest_method    method;
};

static int
well_known_init(void               *priv,
                struct rest_stream *stream,
                enum rest_method    method,
                CURLU              *url __unused)
{
	struct well_known_ctx *ctx = priv;

	ctx->stream = stream;
	ctx->type = XDR_XML;
	ctx->method = method;
	return 0;
}

static int
well_known_header(void          *priv,
                  nghttp2_rcbuf *name __unused,
                  nghttp2_rcbuf *value __unused)
{
	struct well_known_ctx *ctx = priv;

	switch (ctx->method) {
	case METHOD_GET:
	case METHOD_HEAD:
		break;
	case METHOD_OPTIONS:
		return h2c_send_options(ctx->stream, ALLOW_OPTIONS);
	default:
		return h2c_send_error(ctx->stream, 405);
	}

	return h2c_send_error(ctx->stream, 401);
}

static int
well_known_dispatch(void            *priv,
                    struct evbuffer *body __unused)
{
	struct well_known_ctx *ctx = priv;

	printf("%s\n", __func__);
	return h2c_send_error(ctx->stream, 404);
}

static const struct rest_ops well_known_ops = {
	.init     = well_known_init,
	.dispatch = well_known_dispatch,
	.header   = well_known_header
};

ADD_DISPATCHER("/.well-known/", well_known_ops, sizeof(struct well_known_ctx))
