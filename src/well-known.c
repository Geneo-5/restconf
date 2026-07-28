/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#include "restconf/h2c.h"

#define ALLOW_OPTIONS "GET, HEAD, OPTIONS"

#define XRD_ANSWER \
	"<?xml version='1.0' encoding='UTF-8'?>\n" \
	"<XRD xmlns='http://docs.oasis-open.org/ns/xri/xrd-1.0'>" \
		"<Link rel='restconf' href='" CONFIG_H2C_RESTCONF_ROOT "'/>" \
	"</XRD>"

#define JSON_ANSWER \
	"{\"links\":[" \
		"{\"rel\":\"restconf\",\"href\":\"" CONFIG_H2C_RESTCONF_ROOT "\"}" \
	"]}";

enum xdr_type {
	XDR_XML,
	XDR_JSON
};

struct well_known_ctx {
	struct rest_stream *stream;
	enum xdr_type       type;
	enum rest_method    method;
	struct evbuffer    *output;
};

static int
well_known_init(void               *priv,
                struct rest_stream *stream,
                enum rest_method    method,
                CURLU              *url __unused)
{
	struct well_known_ctx *ctx = priv;
	char *path;

	ctx->stream = stream;
	ctx->type = XDR_XML;
	ctx->method = method;
	ctx->output = NULL;

	if (curl_url_get(url, CURLUPART_PATH, &path, 0))
		return -1;

	if (strcmp(path, "/.well-known/host-meta") == 0)
		goto end;

	if (strcmp(path, "/.well-known/host-meta.json") == 0) {
		ctx->type = XDR_JSON;
		goto end;
	}

	curl_free(path);
	return h2c_send_error(ctx->stream, 404);

end:
	curl_free(path);
	return 0;
}

static int
well_known_check_accept(const char *accept, enum xdr_type *type)
{
	int json = 0;
	int xml = 0;

	// check accept default
	if (strstr(accept, "*/*"))
		return 0;

	if (strstr(accept, "application/json"))
		json = 1;

	if (strstr(accept, "application/xdr+xml"))
		xml = 1;

	if (!json && !xml)
		return -1;

	if ((*type == XDR_XML) && xml)
		return 0;
	else if ((*type == XDR_JSON) && json)
		return 0;

	*type = json ? XDR_JSON : XDR_XML;
	return 0;
}

static int
well_known_header(void          *priv,
                  nghttp2_rcbuf *name,
                  nghttp2_rcbuf *value)
{
	struct well_known_ctx *ctx = priv;
	nghttp2_vec            vname = nghttp2_rcbuf_get_buf(name);
	nghttp2_vec            vvalue = nghttp2_rcbuf_get_buf(value);

	if (strcmp((const char *)vname.base, "accept") != 0)
		return 0;

	if (well_known_check_accept((const char *)vvalue.base, &ctx->type))
		return h2c_send_error(ctx->stream, 406);

	return 0;
}

static int
well_known_dispatch(void            *priv,
                    struct evbuffer *body __unused)
{
	struct well_known_ctx *ctx = priv;
	char *out = ctx->type == XDR_XML ? XRD_ANSWER : JSON_ANSWER;
	char *content = ctx->type == XDR_XML ? "application/xrd+xml" :
	                                       "application/json";
	char length[64];
	nghttp2_nv hdrs[3] = {
		MAKE_NV_OK,
		MAKE_NV("content-type", content, strlen(content)),
		MAKE_NV("Content-Length", NULL, 0),
	};

	switch (ctx->method) {
	case METHOD_GET:
	case METHOD_HEAD:
		ctx->output = evbuffer_new();
		if (!ctx->output)
			return NGHTTP2_ERR_NOMEM;

		if (evbuffer_add(ctx->output, out, strlen(out)))
			return NGHTTP2_ERR_NOMEM;

		snprintf(length, sizeof(length), "%zu", evbuffer_get_length(ctx->output));
		hdrs[2].value = (uint8_t *)length;
		hdrs[2].valuelen = strlen(length);
		return h2c_send_answer(ctx->stream, hdrs, 3,
			ctx->method == METHOD_GET ? ctx->output : NULL);
	case METHOD_OPTIONS:
		return h2c_send_options(ctx->stream, ALLOW_OPTIONS);
	default:
		return h2c_send_error(ctx->stream, 405);
	}
}

static void
well_known_fini(void *priv) {
	struct well_known_ctx *ctx = priv;

	evbuffer_free(ctx->output);
}

static const struct rest_ops well_known_ops = {
	.init     = well_known_init,
	.dispatch = well_known_dispatch,
	.header   = well_known_header,
	.fini     = well_known_fini
};

ADD_DISPATCHER("/.well-known/", well_known_ops, sizeof(struct well_known_ctx))
