/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#include "restconf/h2c.h"
#include "restconf/restconf.h"
#include <errno.h>

int
restconf_parse_accept(struct restconf_ctx *ctx, const char *accept)
{

	if (strstr(accept, "application/yang-data+json") ||
	    strstr(accept, "application/yang-patch+json") ||
	    strstr(accept, "application/yang-data+patch+json"))
		ctx->output_format = LYD_JSON;
	else if (strstr(accept, "application/yang-data+xml") ||
	         strstr(accept, "application/yang-patch+xml") ||
	         strstr(accept, "application/yang-data+patch+xml"))
		ctx->output_format = LYD_XML;
	else if (ctx->output_format == LYD_UNKNOWN &&
	         (strstr(accept, "application/*") ||
	          strstr(accept, "*/*")))
		ctx->output_format = LYD_JSON;
	return 0;
}

void
restconf_check_accept(struct restconf_ctx *ctx __unused)
{
	if (ctx->output_format != LYD_UNKNOWN)
		return;

	if (ctx->input_format != LYD_UNKNOWN) {
		ctx->output_format = ctx->input_format;
		return;
	}

	ctx->output_format = LYD_JSON;
}
