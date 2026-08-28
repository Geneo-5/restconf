/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#ifndef _RESTCONF_H
#define _RESTCONF_H

#include "restconf/cdef.h"
#include <sysrepo.h>
#include <sysrepo/error_format.h>

#define REST_FORMAT_JSON (1)
#define REST_FORMAT_XML (1)
#define REST_FORMAT_STREAM (1)

struct restconf_ctx {
	struct rest_stream *stream;
	enum rest_method    method;
	struct evbuffer    *output;
	char               *username;
	sr_datastore_t      datastore;
	sr_session_ctx_t   *session;
	char               *xpath;
	sr_error_info_t    *errors;
	int                 accepted;
	LYD_FORMAT          input_format;
	LYD_FORMAT          output_format;
	char               *options;
	uint32_t            lyd_options;
	int (*dispatch_cd)(struct restconf_ctx *ctx, struct evbuffer *body);
	uint32_t            depth;
	uint32_t            nb_segment;
	uint32_t            oper_opts;
};

int
restconf_parse_path(struct restconf_ctx *ctx, const char *path)
	__rest_nonull(1, 2);

int
restconf_parse_query(struct restconf_ctx *ctx, char *query)
	__rest_nonull(1, 2);

int
restconf_parse_accept(struct restconf_ctx *ctx, const char *accept)
	__rest_nonull(1, 2);

int
restconf_parse_content(struct restconf_ctx *ctx, const char *content)
	__rest_nonull(1, 2);

void
restconf_check_accept(struct restconf_ctx *ctx, struct evbuffer *body)
	__rest_nonull(1, 2);

static inline LYD_FORMAT __rest_nonull(1)
restconf_get_format(struct restconf_ctx *ctx)
{
	return ctx->output_format;
}

static inline LYD_FORMAT __rest_nonull(1)
restconf_post_format(struct restconf_ctx *ctx)
{
	return ctx->input_format;
}

int
restconf_start_session(struct restconf_ctx *ctx)
	__rest_nonull(1);

int
restconf_force_session(struct restconf_ctx *ctx, sr_datastore_t datastore)
	__rest_nonull(1);
int
restconf_send_error(struct restconf_ctx *ctx, const sr_error_info_t *errors)
	__rest_nonull(1, 2);


int
restconf_send_answer(struct restconf_ctx *ctx, const struct lyd_node *root)
	__rest_nonull(1);

int
restconf_not_content_answer(struct restconf_ctx *ctx)
	__rest_nonull(1);

static inline uint32_t
restconf_get_depth(struct restconf_ctx *ctx, uint32_t shift)
{
	uint32_t depth = 0;

	if (!ctx->depth)
		return depth;

	if (ctx->depth <= shift)
		return (uint32_t)-1;

	return ctx->depth - shift + ctx->nb_segment;
}

static inline uint32_t
restconf_get_oper_opts(struct restconf_ctx *ctx)
{
	if (ctx->datastore != SR_DS_OPERATIONAL)
		return 0;

	return ctx->oper_opts;
}

#endif /* _RESTCONF_H */
