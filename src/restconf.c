/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#include "restconf/h2c.h"
#include "restconf/restconf.h"
#include <errno.h>

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
	ctx->errors   = NULL;

	if (curl_url_get(url, CURLUPART_PATH, &path, 0))
		return -1;

	ret = restconf_parse_path(ctx, path);
	curl_free(path);
	return ret;
}

static void
restconf_fini(void *priv) {
	struct restconf_ctx *ctx = priv;

	free(ctx->username);
	free(ctx->xpath);
	sr_session_stop(ctx->session);
	srplg_errinfo_free(&ctx->errors);
	if (ctx->output)
		evbuffer_free(ctx->output);

}

static int
restconf_header(void          *priv,
                nghttp2_rcbuf *name,
                nghttp2_rcbuf *value)
{
	struct restconf_ctx *ctx = priv;
	nghttp2_vec          vname = nghttp2_rcbuf_get_buf(name);
	nghttp2_vec          vvalue = nghttp2_rcbuf_get_buf(value);

	if (strcmp((const char *)vname.base, "accept") == 0)
		return restconf_parse_accept(ctx, (const char *)vvalue.base);

	return 0;
}

static int
restconf_identified(void *priv, const char *name)
{
	struct restconf_ctx *ctx = priv;

	ctx->username = strdup(name);
	return ctx->username ? 0 : -ENOMEM;
}

static int
restconf_dispatch(void            *priv __unused,
                  struct evbuffer *body __unused)
{
	struct restconf_ctx *ctx = priv;
	sr_conn_ctx_t       *conn = h2c_stream_get_conn(ctx->stream);
	sr_data_t           *data = NULL;
	int                  ret;

	printf("%p user %p: %s\n", ctx->errors, ctx->username, ctx->username);
	if (!ctx->username) {
		if (ctx->errors)
			srplg_errinfo_free(&ctx->errors);
		srplg_errinfo_set_netconf_error(&ctx->errors, "transport",
			"access-denied", NULL, NULL, "access-denied", 0);
	}

	restconf_check_accept(ctx);

	printf("error, %p\n", ctx->errors);
	if (ctx->errors)
		return restconf_send_error(ctx, ctx->errors);

	sr_session_start(conn, ctx->datastore, &ctx->session);
	sr_session_set_user(ctx->session, ctx->username);

	sr_get_data(ctx->session, ctx->xpath, 0, 0, 0, &data);
	ret = restconf_send_answer(ctx, data->tree);
	sr_release_data(data);
	return ret;
}

static const struct rest_ops restconf_ops = {
	.init       = restconf_init,
	.header     = restconf_header,
	.identified = restconf_identified,
	.dispatch   = restconf_dispatch,
	.fini       = restconf_fini
};

ADD_DISPATCHER(CONFIG_H2C_RESTCONF_ROOT, restconf_ops, sizeof(struct restconf_ctx))
