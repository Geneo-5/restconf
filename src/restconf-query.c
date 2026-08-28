/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#include "restconf/h2c.h"
#include "restconf/restconf.h"
#include <errno.h>
#include <utils/string.h>

#define REST_QUERY_CONTENT        0x001
#define REST_QUERY_DEPTH          0x002
#define REST_QUERY_FIELDS         0x004
#define REST_QUERY_FILTER         0x008
#define REST_QUERY_INSERT         0x010
#define REST_QUERY_POINT          0x020
#define REST_QUERY_START_TIME     0x040
#define REST_QUERY_STOP_TIME      0x080
#define REST_QUERY_WITH_DEFAULTS  0x100
#define REST_QUERY_WITH_ORIGIN    0x200

#define REST_GET_QUERY (REST_QUERY_CONTENT | \
                        REST_QUERY_DEPTH | \
                        REST_QUERY_FILTER | \
                        REST_QUERY_START_TIME | \
                        REST_QUERY_STOP_TIME | \
                        REST_QUERY_WITH_DEFAULTS | \
                        REST_QUERY_WITH_ORIGIN)

#define REST_POST_QUERY (REST_QUERY_INSERT | REST_QUERY_POINT)

struct query_cb_list {
	const char *key;
	int (*query_cb)(struct restconf_ctx *ctx, const char *value);
	uint32_t type;
};

static int
content_query(struct restconf_ctx *ctx, const char *value)
{
	if (!value)
		goto error;

	ctx->oper_opts &= (uint32_t)~(SR_OPER_NO_STATE | SR_OPER_NO_CONFIG);
	if (strcmp(value, "all") == 0)
		ctx->oper_opts |= 0;
	else if (strcmp(value, "config") == 0)
		ctx->oper_opts |= SR_OPER_NO_STATE;
	else if (strcmp(value, "nonconfig") == 0)
		ctx->oper_opts |= SR_OPER_NO_CONFIG;
	else
		goto error;

	return 0;
error:
	srplg_errinfo_set_netconf_error(&ctx->errors, "protocol", "invalid-value",
		NULL, NULL, "Queriy content must be a string \"config\", \"nonconfig\" or \"all\"", 0);
	return -EINVAL;
}

static int
depth_query(struct restconf_ctx *ctx, const char *value)
{
	int err;
	unsigned long depth;

	if (!value)
		goto error;

	if (strcmp(value, "unbounded") == 0) {
		ctx->depth = 0;
		return 0;
	}

	err = ustr_parse_base_ulong(value, &depth, 10);
	if (err)
		goto error;

	if ((depth < 1) || (depth > 65535))
		goto error;

	ctx->depth = (uint32_t)depth;
	return 0;
error:
	srplg_errinfo_set_netconf_error(&ctx->errors, "protocol", "invalid-value",
		NULL, NULL, "Queriy depth must be an integer between 1 and 65535 or the string \"unbounded\"", 0);
	return -EINVAL;
}

static int
fields_query(struct restconf_ctx *ctx __unused, const char *value __unused)
{
	return 0;
}

static int
filter_query(struct restconf_ctx *ctx __unused, const char *value __unused)
{
	return 0;
}

static int
insert_query(struct restconf_ctx *ctx __unused, const char *value __unused)
{
	return 0;
}

static int
point_query(struct restconf_ctx *ctx __unused, const char *value __unused)
{
	return 0;
}

static int
start_query(struct restconf_ctx *ctx __unused, const char *value __unused)
{
	return 0;
}

static int
stop_query(struct restconf_ctx *ctx __unused, const char *value __unused)
{
	return 0;
}

static int
defaults_query(struct restconf_ctx *ctx __unused, const char *value __unused)
{
	if (!value)
		goto error;

	ctx->lyd_options &= (uint32_t)~LYD_PRINT_WD_MASK;
	if (strcmp(value, "report-all") == 0)
		ctx->lyd_options |= LYD_PRINT_WD_ALL;
	else if (strcmp(value, "trim") == 0)
		ctx->lyd_options |= LYD_PRINT_WD_TRIM;
	else if (strcmp(value, "explicit") == 0)
		ctx->lyd_options |= LYD_PRINT_WD_EXPLICIT;
	else if (strcmp(value, "report-all-tagged") == 0)
		ctx->lyd_options |= LYD_PRINT_WD_ALL_TAG;
	else
		goto error;

	return 0;
error:
	srplg_errinfo_set_netconf_error(&ctx->errors, "protocol", "invalid-value",
		NULL, NULL, "Queriy with-defaults must be a string \"report-all\", \"trim\", \"explicit\" or \"report-all-tagged\"", 0);
	return -EINVAL;
}

static int
origin_query(struct restconf_ctx *ctx, const char *value)
{
	if (value || (ctx->datastore != SR_DS_OPERATIONAL))
		goto error;

	ctx->oper_opts |= SR_OPER_WITH_ORIGIN;
	return 0;
error:
	srplg_errinfo_set_netconf_error(&ctx->errors, "protocol", "invalid-value",
		NULL, NULL, "Invalid query with-origin", 0);
	return -EINVAL;
}

static const struct query_cb_list query_list[] = {
	{"content",       content_query,  REST_QUERY_CONTENT},
	{"depth",         depth_query,    REST_QUERY_DEPTH},
	{"fields",        fields_query,   REST_QUERY_FIELDS},
	{"filter",        filter_query,   REST_QUERY_FILTER},
	{"insert",        insert_query,   REST_QUERY_INSERT},
	{"point",         point_query,    REST_QUERY_POINT},
	{"start-time",    start_query,    REST_QUERY_START_TIME},
	{"stop-time",     stop_query,     REST_QUERY_STOP_TIME},
	{"with-defaults", defaults_query, REST_QUERY_WITH_DEFAULTS},
	{"with-origin",   origin_query,   REST_QUERY_WITH_ORIGIN},
};

static ssize_t
restconf_parse_one_query(struct restconf_ctx *ctx, char *query, uint32_t *mask)
{
	char *tokens = NULL;
	char *msg = NULL;
	char *key = strtok_r(query, "=", &tokens);
	char *value = strtok_r(NULL, "=", &tokens);

	for (size_t i = 0; i < stroll_array_nr(query_list); i++) {
		if (strcmp(query, query_list[i].key))
			continue;

		if (*mask & query_list[i].type) {
			*mask &= ~query_list[i].type;
			return query_list[i].query_cb(ctx, value);
		}

		asprintf(&msg, "unexpected query %s", key);
		goto err;
	}

	asprintf(&msg, "unkown query %s", key);
err:
	srplg_errinfo_set_netconf_error(&ctx->errors, "protocol", "invalid-value",
		NULL, NULL, msg, 0);
	free(msg);
	return -1;
}

int
restconf_parse_query(struct restconf_ctx *ctx, char *query)
{
	char    *token;
	char    *tokens = NULL;
	uint32_t  mask = 0;


	pr_dbg("query %s", query);
	switch (ctx->method) {
	case METHOD_HEAD:
	case METHOD_GET:
		mask = REST_GET_QUERY;
		break;
	case METHOD_POST:
	case METHOD_PUT:
		mask = REST_POST_QUERY;
		break;
	default:
		goto err;

	}

	token = strtok_r(query, "&", &tokens);
	while (token != NULL) {
		if (restconf_parse_one_query(ctx, token, &mask))
			return 0;

		token = strtok_r(NULL, "&", &tokens);
	}

	return 0;

err:
	srplg_errinfo_set_netconf_error(&ctx->errors, "protocol", "invalid-value",
		NULL, NULL, "Queries not supported for this method", 0);
	return 0;
}
