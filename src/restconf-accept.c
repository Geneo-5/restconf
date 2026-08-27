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
	ctx->accepted = 1;
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

int
restconf_parse_content(struct restconf_ctx *ctx, const char *content)
{
	if (strstr(content, "application/yang-data+json") ||
	    strstr(content, "application/yang-patch+json") ||
	    strstr(content, "application/yang-data+patch+json"))
		ctx->input_format = LYD_JSON;
	else if (strstr(content, "application/yang-data+xml") ||
	         strstr(content, "application/yang-patch+xml") ||
	         strstr(content, "application/yang-data+patch+xml"))
		ctx->input_format = LYD_XML;
	else
		srplg_errinfo_set_netconf_error(&ctx->errors, "transport",
			"invalid-value", NULL, NULL, "Content-Type not supported", 0);

	return 0;
}

void
restconf_check_accept(struct restconf_ctx *ctx, struct evbuffer *body)
{
	switch (ctx->method) {
	case METHOD_POST:
	case METHOD_PUT:
	case METHOD_PATCH:
		if (evbuffer_get_length(body) && (ctx->input_format == LYD_UNKNOWN)) {
			srplg_errinfo_set_netconf_error(&ctx->errors, "transport",
				"invalid-value", NULL, NULL, "Missing Content-Type", 0);
			return;
		}
		break;
	default:
		break;
	}

	if (ctx->output_format != LYD_UNKNOWN)
		return;

	if (ctx->input_format != LYD_UNKNOWN) {
		ctx->output_format = ctx->input_format;
		return;
	}

	if (ctx->accepted) {
		srplg_errinfo_set_netconf_error(&ctx->errors, "transport",
			"invalid-value", NULL, NULL, "Media not supported", 0);
		return;
	}

	ctx->output_format = LYD_JSON;
}
