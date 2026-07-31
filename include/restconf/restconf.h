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
};

int
restconf_parse_path(struct restconf_ctx *ctx, const char *path)
	__rest_nonull(1, 2);

int
restconf_parse_accept(struct restconf_ctx *ctx, const char *accept)
	__rest_nonull(1, 2);

void
restconf_check_accept(struct restconf_ctx *ctx)
	__rest_nonull(1);

LYD_FORMAT
restconf_get_format(struct restconf_ctx *ctx)
	__rest_nonull(1);

int
restconf_send_error(struct restconf_ctx *ctx, sr_error_info_t *errors)
	__rest_nonull(1, 2);


int
restconf_send_error(struct restconf_ctx *ctx, sr_error_info_t *errors)
	__rest_nonull(1, 2);

int
restconf_send_answer(struct restconf_ctx *ctx, const struct lyd_node *root)
	__rest_nonull(1, 2);

#endif /* _RESTCONF_H */
