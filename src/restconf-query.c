/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#include "restconf/h2c.h"
#include "restconf/restconf.h"
#include <errno.h>

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
content_query(struct restconf_ctx *ctx __unused, const char *value __unused)
{
	return 0;
}

static int
depth_query(struct restconf_ctx *ctx __unused, const char *value __unused)
{
	return 0;
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
	return 0;
}

static int
origin_query(struct restconf_ctx *ctx __unused, const char *value __unused)
{
	return 0;
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

	pr_dbg("\t %s : %s", key, value);
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
	pr_dbg("query error %s", msg);
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
