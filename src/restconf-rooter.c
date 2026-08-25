/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#include "restconf/h2c.h"
#include "restconf/restconf.h"
#include <errno.h>

static void
get_yang_library_version(struct restconf_ctx *ctx, char *buf, size_t buf_len)
{
	const struct ly_ctx *ly_ctx;
	const struct lys_module *mod;
	const char *fallback = "2016-06-21";

	ly_ctx = sr_acquire_context(h2c_stream_get_conn(ctx->stream));
	mod = ly_ctx_get_module_implemented(ly_ctx, "ietf-yang-library");
	if (mod && mod->revision && mod->revision[0] != '\0')
		snprintf(buf, buf_len, "%s", mod->revision);
	else
		snprintf(buf, buf_len, "%s", fallback);
	sr_release_context(h2c_stream_get_conn(ctx->stream));
}

static int
resconf_root_answer(struct restconf_ctx *ctx, struct evbuffer *body __unused)
{
	struct lyd_node *root = NULL;
	const struct ly_ctx *ly_ctx;
	const struct lys_module *nc_mod;
	struct lyd_node *restconf = NULL;
	char version[11];
	int ret;

	ctx->lyd_options |= LYD_PRINT_EMPTY_CONT;
	get_yang_library_version(ctx, version, sizeof(version));
	ly_ctx = sr_acquire_context(h2c_stream_get_conn(ctx->stream));
	nc_mod = ly_ctx_get_module_implemented(ly_ctx, "ietf-restconf");
	rest_assert(nc_mod);
	lyd_new_inner(NULL, nc_mod, "restconf", 0, &restconf);
	lyd_new_inner(restconf, NULL, "data", 0, NULL);
	lyd_new_inner(restconf, NULL, "operations", 0, NULL);
	lyd_new_term(restconf, NULL, "yang-library-version", version, 0, NULL);
	lyd_merge_siblings(&root, restconf, 0);
	lyd_free_siblings(restconf);
	sr_release_context(h2c_stream_get_conn(ctx->stream));
	ret = restconf_send_answer(ctx, root);
	lyd_free_siblings(root);
	return ret;
}

static int
resconf_root_data_answer(struct restconf_ctx *ctx, struct evbuffer *body __unused)
{
	struct lyd_node *root = NULL;
	const struct ly_ctx *ly_ctx;
	struct lyd_node *restconf_data = NULL;
	sr_data_t *data = NULL;
	int ret;

	ly_ctx = sr_acquire_context(h2c_stream_get_conn(ctx->stream));
	lyd_new_path(NULL, ly_ctx, "/ietf-restconf:restconf", NULL, 0, &root);
	lyd_new_inner(root, NULL, "data", 0, &restconf_data);

	restconf_start_session(ctx);
	sr_get_data(ctx->session, "/*", 0, 0, 0, &data);
	lyd_dup_siblings(data->tree, restconf_data, LYD_DUP_RECURSIVE, NULL);
	sr_release_data(data);

	// lyd_print_fd(1, root, LYD_JSON, LYD_PRINT_SIBLINGS | LYD_PRINT_WD_ALL);

	sr_release_context(h2c_stream_get_conn(ctx->stream));
	ret = restconf_send_answer(ctx, restconf_data);
	lyd_free_siblings(root);
	return ret;
}

static int
resconf_root_operations_answer(struct restconf_ctx *ctx __unused, struct evbuffer *body __unused)
{
	const struct ly_ctx *ly_ctx;
	char length[64];
	const char *sep = "";
	uint32_t idx = 0;
	const struct lys_module *module;
	LYD_FORMAT ly_fmt = restconf_get_format(ctx);
	nghttp2_nv hdrs[] = {
		MAKE_NV_OK,
		MAKE_NV_TEMP("content-type"),
		MAKE_NV_TEMP("content-length"),
	};

	set_nv(&hdrs[1], ly_fmt == LYD_JSON ? CONTENT_JSON : CONTENT_XML);
	rest_assert(!ctx->output);
	ctx->output = evbuffer_new();
	if (!ctx->output)
		return -ENOMEM;

	evbuffer_add_printf(ctx->output, "%s", ly_fmt == LYD_JSON ?
		"{\"operations\":[" :
		"<operations xmlns=\"urn:ietf:params:xml:ns:yang:ietf-restconf\">");
	ly_ctx = sr_acquire_context(h2c_stream_get_conn(ctx->stream));

	while ((module = ly_ctx_get_module_iter(ly_ctx, &idx))) {
		const struct lysc_node_action *node = NULL;

		if (!module->implemented)
			continue;

		LY_LIST_FOR(module->compiled->rpcs, node) {
			if (node->nodetype == LYS_RPC) {
				if (ly_fmt == LYD_JSON)
					evbuffer_add_printf(ctx->output,
						"%s{\"%s:%s\":[null]}",
						sep, module->name, node->name);
				else
					evbuffer_add_printf(ctx->output,
						"<%s xmlns='urn:ietf:params:xml:ns:yang:%s'/>",
						node->name, module->name);
				sep = ",";
			}
		}
	}

	sr_release_context(h2c_stream_get_conn(ctx->stream));
	evbuffer_add_printf(ctx->output, "%s", ly_fmt == LYD_JSON ? "]}" : "</operations>");
	snprintf(length, sizeof(length), "%zu", evbuffer_get_length(ctx->output));
	set_nv(&hdrs[2], length);
	return h2c_send_answer(ctx->stream, hdrs, stroll_array_nr(hdrs),
	                       ctx->method == METHOD_HEAD ? NULL : ctx->output);
}

static int
restconf_options(struct restconf_ctx *ctx, struct evbuffer *body __unused)
{
	return h2c_send_options(ctx->stream, ctx->options);
}

static int
resconf_get_data(struct restconf_ctx *ctx, struct evbuffer *body __unused)
{
	sr_data_t *data = NULL;
	const sr_error_info_t *errors;
	int ret;

	if (restconf_start_session(ctx))
		goto error;

	ret = sr_get_data(ctx->session, ctx->xpath, 0, 0, 0, &data);
	if (ret)
		goto error;

	ret = restconf_send_answer(ctx, data ? data->tree : NULL);
	sr_release_data(data);
	return ret;

error:
	sr_session_get_error(ctx->session, &errors);
	return restconf_send_error(ctx, errors);
}

static int
resconf_update_data(struct restconf_ctx *ctx __unused, struct evbuffer *body __unused)
{
	return 0;
}

static int
resconf_rpc(struct restconf_ctx *ctx __unused, struct evbuffer *body __unused)
{
	return 0;
}

static int
check_get_method(struct restconf_ctx *ctx)
{
	return (ctx->method != METHOD_GET) &&
	       (ctx->method != METHOD_HEAD) &&
	       (ctx->method != METHOD_OPTIONS);
}

static int
check_post_method(struct restconf_ctx *ctx)
{
	ctx->options = "POST, OPTIONS";
	return (ctx->method != METHOD_POST) &&
	       (ctx->method != METHOD_OPTIONS);
}

static ssize_t
parse_segment(struct restconf_ctx *ctx,
              const char          *path,
              size_t               len,
              char                *xpath,
              size_t               max_len)
{
	ssize_t written;
	size_t length = len;
	const struct ly_ctx *ly_ctx;
	const struct lysc_node *node;
	char *str = NULL;
	char *eq_pos = strchr(path, '=');
	char *decoded;

	if (eq_pos && (eq_pos > (path + len)))
		eq_pos = NULL;

	rest_assert(max_len > len);

	length = eq_pos ? (size_t)(eq_pos - path) : length;
	decoded = curl_unescape(path, (int)length);
	written = snprintf(xpath, max_len, "%s", decoded);
	if (written < 0)
		goto err;

	curl_free(decoded);
	length   = (size_t)written;
	xpath   += length;
	max_len -= length;

	ly_ctx = sr_acquire_context(h2c_stream_get_conn(ctx->stream));
	node = lys_find_path(ly_ctx, NULL, ctx->xpath, 0);
	sr_release_context(h2c_stream_get_conn(ctx->stream));
	if (!node)
		goto err;

	if (!eq_pos)
		return (ssize_t)length;

	str = strndup(eq_pos + 1, len - length - 1);
	switch (node->nodetype) {
	case LYS_LEAFLIST:
		decoded = curl_unescape(str, (int)strlen(str));
		written = snprintf(xpath, max_len, "[.='%s']", decoded);
		curl_free(decoded);
		if (written < 0)
			goto err;

		length += (size_t)written;
		break;
	case LYS_LIST:
		const struct lysc_node_list *list = (const struct lysc_node_list *)node;
		const struct lysc_node *key = list->child;
		char *ptr = str;

		while (key && ptr) {
			if (key->flags & LYS_KEY) {
				char *comma = strchr(ptr, ',');

				if (comma)
					comma[0] = '\0';

				if (ptr == comma)
					goto next;

				decoded = curl_unescape(str, (int)strlen(str));
				written = snprintf(xpath, max_len, "[%s='%s']", key->name, decoded);
				curl_free(decoded);
				if (written < 0)
					goto err;

				length  += (size_t)written;
				xpath   += (size_t)written;
				max_len -= (size_t)written;
				if (!comma)
					break;
next:
				ptr = comma + 1;
			}
			key = key->next;
		}
		break;
	default:
		goto err;
	}

	free(str);
	return (ssize_t)length;
err:
	free(str);
	return -1;
}

static void
make_xpath(struct restconf_ctx *ctx, const char *path)
{
	const char *ppts = path;
	size_t      max_len = 4096;
	char       *xpts;

	ctx->xpath = malloc(4096);
	xpts = ctx->xpath;

	if (!ctx->xpath) {
		srplg_errinfo_set_netconf_error(&ctx->errors, "application", "operation-failed", NULL, NULL, NULL, 0);
		return;
	}

	// pr_dbg("path %s", path);
	while(ppts[0] == '/') {
		char *seg_end = strchr(ppts + 1, '/');
		size_t seg_len = seg_end ? (size_t)(seg_end - ppts) : strlen(ppts);
		ssize_t len;

		if (seg_len == 0)
			goto error_404;

		len = parse_segment(ctx, ppts, seg_len, xpts, max_len);
		if (len < 0)
			goto error_404;

		xpts    += (size_t)len;
		max_len -= (size_t)len;
		ppts    += seg_len;
	}

	return;

error_404:
	srplg_errinfo_set_netconf_error(&ctx->errors, "protocol", "invalid-value", NULL, NULL, "Invalid path", 0);
	free(ctx->xpath);
	ctx->xpath = NULL;
}

int
restconf_parse_path(struct restconf_ctx *ctx, const char *path)
{
	ctx->options = "GET, HEAD, OPTIONS";
	if ((strcmp(path, "/restconf") == 0) ||
	    (strcmp(path, "/restconf/") == 0)) {
		ctx->options = "GET, HEAD, OPTIONS";
		ctx->xpath = strdup("/ietf-restconf:restconf");
		ctx->dispatch_cd = resconf_root_answer;
		if (check_get_method(ctx))
			goto invalid_method;

	} else if ((strcmp(path, "/restconf/ds") == 0) ||
	           (strcmp(path, "/restconf/ds/") == 0)) {
		ctx->options = "GET, HEAD, OPTIONS";
		ctx->datastore = SR_DS_OPERATIONAL;
		ctx->dispatch_cd = resconf_get_data;
		ctx->xpath = strdup("/ietf-yang-library:yang-library/datastore");
		if (check_get_method(ctx))
			goto invalid_method;

	} else if ((strcmp(path, "/restconf/data") == 0) ||
	           (strcmp(path, "/restconf/data/") == 0)) {
		ctx->options = "GET, HEAD, OPTIONS";
		ctx->xpath = strdup("/ietf-restconf:restconf/data");
		ctx->dispatch_cd = resconf_root_data_answer;
		ctx->datastore = SR_DS_OPERATIONAL;
		if (check_get_method(ctx))
			goto invalid_method;

	} else if ((strcmp(path, "/restconf/operations") == 0) ||
	           (strcmp(path, "/restconf/operations/") == 0)) {
		ctx->options = "GET, HEAD, POST, OPTIONS";
		ctx->dispatch_cd = resconf_root_operations_answer;
		ctx->datastore = SR_DS_OPERATIONAL;
		if (check_get_method(ctx))
			goto invalid_method;

	} else if (strncmp(path, "/restconf/data/", 15) == 0) {
		ctx->options = "GET, HEAD, POST, PUT, PATCH, DELETE, OPTIONS";
		make_xpath(ctx, path + 14);
		// ctx->xpath = strdup(path + 14);
		// pr_dbg("%s -> %s", path, ctx->xpath);
		if (check_get_method(ctx) == 0) {
			ctx->datastore = SR_DS_OPERATIONAL;
			ctx->dispatch_cd = resconf_get_data;
		} else {
			ctx->datastore = SR_DS_RUNNING;
			ctx->dispatch_cd = resconf_update_data;
		}
	} else if (strncmp(path, "/restconf/operations/", 21) == 0) {
		ctx->options = "POST, OPTIONS";
		ctx->datastore = SR_DS_OPERATIONAL;
		ctx->dispatch_cd = resconf_rpc;
		ctx->xpath = strdup(path + 20);
		if (check_post_method(ctx))
			goto invalid_method;
	} else
		goto err;

	if (ctx->method == METHOD_OPTIONS)
		ctx->dispatch_cd = restconf_options;

	return 0;

err:
	srplg_errinfo_set_netconf_error(&ctx->errors, "protocol", "invalid-value",
		NULL, NULL, "Invalid path", 0);
	return 0;

invalid_method:
	srplg_errinfo_set_netconf_error(&ctx->errors, "protocol", "operation-not-supported",
		NULL, NULL, "Invalid method", 0);
	return 0;
}
