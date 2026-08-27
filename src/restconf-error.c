/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#include "restconf/h2c.h"
#include "restconf/restconf.h"

static ssize_t
restconf_write_cb(void *priv, const void *buf, size_t count)
{
	struct restconf_ctx *ctx = priv;

	if (evbuffer_add(ctx->output, buf, count))
		return -ENOMEM;

	return (ssize_t)count;
}

static void
restconf_set_status(struct restconf_ctx *ctx,
                    const char          *tag,
                    const char          *msg,
                    nghttp2_nv          *nv,
                    size_t              *nb)
{
	switch (strlen(tag)) {
	case 6:
		rest_assert(strcmp(tag, "in-use") == 0);
		set_nv(nv, "409");
		return;
	case 7:
		rest_assert(strcmp(tag, "too-big") == 0);
		set_nv(nv, "413");
		return;
	case 11:
		if (strcmp(tag, "bad-element") == 0) {
			set_nv(nv, "400");
			return;
		}

		if (strcmp(tag, "lock-denied") == 0) {
			set_nv(nv, "409");
			return;
		}

		if (strcmp(tag, "data-exists") == 0) {
			set_nv(nv, "409");
			return;
		}
		break;
	case 12:
		rest_assert(strcmp(tag, "data-missing") == 0);
		set_nv(nv, "404");
		return;

	case 13:
		if (strcmp(tag, "invalid-value") == 0) {
			// 400, parfois 404 ou 406 selon contexte
			// Valeur invalide, ressource inexistante,
			// représentation invalide
			if (strstr(msg, "Content-Type"))
				set_nv(nv, "415");
			else if (strcmp(msg, "Media not supported") == 0)
				set_nv(nv, "406");
			else if (!ctx->xpath)
				set_nv(nv, "404");
			else
				set_nv(nv, "400");
			return;
		}

		if (strcmp(tag, "bad-attribute") == 0) {
			set_nv(nv, "400");
			return;
		}

		if (strcmp(tag, "access-denied") == 0) {
			set_nv(nv, ctx->username ? "403" : "401");
			make_nv(&nv[*nb], WWW_AUTH, ctx->username ? WWW_AUTH_403 : WWW_AUTH_401);
			*nb = *nb + 1;
			return;
		}
		break;

	case 15:
		if ((strcmp(tag, "missing-element") == 0)) {
			set_nv(nv, "400");
			return;
		}

		if ((strcmp(tag, "unknown-element") == 0)) {
			set_nv(nv, "400");
			return;
		}

		if ((strcmp(tag, "resource-denied") == 0)) {
			set_nv(nv, "403");
			make_nv(&nv[*nb], WWW_AUTH, WWW_AUTH_403);
			*nb = *nb + 1;
			return;
		}

		if ((strcmp(tag, "rollback-failed") == 0)) {
			set_nv(nv, "500");
			return;
		}
		break;

	case 16:
		rest_assert(strcmp(tag, "operation-failed") == 0);
		// 500, parfois 412 selon contexte | Échec logique de
		// l’opération, validation métier, exécution RPC,
		// échec de précondition
		set_nv(nv, "500");
		return;
	case 17:
		rest_assert(strcmp(tag, "missing-attribute") == 0 ||
		            strcmp(tag, "unknown-attribute") == 0 ||
		            strcmp(tag, "unknown-namespace") == 0 ||
		            strcmp(tag, "rollback-failed") == 0);
		set_nv(nv, "400");
		return;
	case 23:
		rest_assert(strcmp(tag, "operation-not-supported") == 0);
		// 405, parfois 501 selon contexte | Méthode ou opération non
		// supportée ; `405` doit inclure `Allow`
		set_nv(nv, "405");
		if (ctx->options) {
			make_nv(&nv[*nb], "allow", ctx->options);
			*nb = *nb + 1;
		}
		return;
	}

	rest_assert(0);
	set_nv(nv, "500");
}

static char *
np_err_reply_get_quoted_string(const char *msg, uint32_t index)
{
    const char *start = NULL, *end = NULL, *iter, *tmp;
    uint32_t quote_cnt = 0, last_quote;

    assert(msg);

    last_quote = (index + 1) * 2;
    for (iter = msg; *iter; ++iter) {
        if (*iter != '\"') {
            continue;
        }
        /* updating the start and end pointers - swap */
        tmp = end;
        end = iter;
        start = tmp;
        if (++quote_cnt == last_quote) {
            /* nth substring found */
            break;
        }
    }

    if (!start) {
        return NULL;
    }

    /* skip the first quote */
    ++start;

    /* copy substring */
    return strndup(start, (size_t)(end - start));
}

static void
sr_err_to_netconf_error(struct restconf_ctx        *ctx,
                        const sr_error_info_err_t  *err,
                        sr_error_info_t           **net_err)
{
	const struct  ly_ctx *ly_ctx;
	const struct  lysc_node *cn;
	const char   *msg;
	char         *path = NULL;
	char         *ptr;
	char         *str = NULL;
	char         *str2 = NULL;


	msg = err->message ? err->message : sr_strerror(err->err_code);
	if ((ptr = strstr(msg, "(path \"")))
		ptr += 7;

	if (ptr)
		path = strndup(ptr, (size_t)(strchr(ptr, '\"') - ptr));

	// *error_info_elements = malloc(2 * sizeof(**error_info_elements));
	// *error_info_values = malloc(2 * sizeof(**error_info_values));
	// *error_info_count = 0;

	if (!strncmp(msg, "Unique data leaf(s)", 19))
		srplg_errinfo_set_netconf_error(net_err,
			"protocol", "operation-failed", "data-not-unique",
			NULL, "Unique constraint violated.", 1,
			"non-unique", path);
	else if (!strncmp(msg, "Too many", 8))
		srplg_errinfo_set_netconf_error(net_err,
			"protocol", "operation-failed", "too-many-elements",
			path, "Too many elements.", 0);
	else if (!strncmp(msg, "Too few", 7))
		srplg_errinfo_set_netconf_error(net_err,
			"protocol", "operation-failed", "too-few-elements",
			path, "Too few elements.", 0);
	else if (!strncmp(msg, "Must condition", 14)) {
		ptr = strrchr(msg, '(');
		--ptr;
		str = strndup(msg, (size_t)(ptr - msg));
		srplg_errinfo_set_netconf_error(net_err,
			"protocol", "operation-failed", "must-violation",
			path, str, 0);
	} else if ((!strncmp(msg, "Invalid leafref value", 21)) && strstr(msg, "no target instance")) {
		str = np_err_reply_get_quoted_string(msg, 0);
		asprintf(&str2, "Required leafref target with value \"%s\" missing.", str);
		srplg_errinfo_set_netconf_error(net_err,
			"protocol", "data-missing", "instance-required",
			path, str2, 0);
	} else if (!strncmp(msg, "Invalid instance-identifier", 26) && strstr(msg, "required instance not found")) {
		str = np_err_reply_get_quoted_string(msg, 0);
		asprintf(&str2, "Required instance-identifier \"%s\" missing.", str);
		srplg_errinfo_set_netconf_error(net_err,
			"protocol", "data-missing", "instance-required",
			path, str2, 0);
	} else if (!strncmp(msg, "Mandatory choice", 16)) {
		str = np_err_reply_get_quoted_string(msg, 0);
		srplg_errinfo_set_netconf_error(net_err,
			"protocol", "data-missing", "mandatory-choice",
			path, "Missing mandatory choice.", 1,
			"missing-choice", str);
	} else if (strstr(msg, "instance to insert next to not found.")) {
		str = np_err_reply_get_quoted_string(msg, 0);
		asprintf(&str2, "Missing insert anchor \"%s\" instance.", str);
		srplg_errinfo_set_netconf_error(net_err,
			"protocol", "bad-attribute", "missing-instance",
			NULL, str2, 0);
	} else if (!strncmp(msg, "Invalid non-", 12) || !strncmp(msg, "Invalid type", 12) ||
		!strncmp(msg, "Unsatisfied range", 17) || !strncmp(msg, "Unsatisfied pattern", 19) ||
		strstr(msg, "min/max bounds")) {
		ptr = strrchr(msg, '(');
		if (ptr) {
			str = strndup(msg, (size_t)((ptr - 1) - msg));
		}
		ptr = strrchr(path, ':');
		if (!ptr) {
			ptr = strrchr(path, '/');
		}
		++ptr;
		srplg_errinfo_set_netconf_error(net_err,
			"application", "bad-element", NULL,
			path, str ? str : msg, 1,
			"bad-element", ptr);
	} else if (!strncmp(msg, "Node \"", 6) && strstr(msg, " not found")) {
		str = np_err_reply_get_quoted_string(msg, 0);
		srplg_errinfo_set_netconf_error(net_err,
			"application", "unknown-element", NULL,
			NULL, msg, 1,
			"bad-element", str);
	} else if (!strncmp(msg, "No (implemented) module with namespace", 38)) {
		str = np_err_reply_get_quoted_string(msg, 0);
		str2 = np_err_reply_get_quoted_string(msg, 1);
		srplg_errinfo_set_netconf_error(net_err,
			"application", "unknown-namespace", NULL,
			NULL, "An unexpected namespace is present.", 2,
			"bad-element", str2,
			"bad-namespace", str);
	} else if (!strncmp(msg, "Mandatory node", 14)) {
		const char *type;

		str = np_err_reply_get_quoted_string(msg, 0);
		ly_ctx = sr_acquire_context(h2c_stream_get_conn(ctx->stream));
		cn = lys_find_path(ly_ctx, NULL, path, 0);
		if (cn && ((cn->nodetype & LYS_RPC) || (cn->nodetype & LYS_INPUT)))
			type = "protocol";
		else
			type = "application";

		sr_release_context(h2c_stream_get_conn(ctx->stream));
		srplg_errinfo_set_netconf_error(net_err,
			type, "missing-element", NULL,
			path, "An expected element is missing.", 1,
			"bad-element", str);
	} else if ((ptr = strstr(msg, "DS-locked by session "))) {
		// case commit ? session-id ?
		srplg_errinfo_set_netconf_error(net_err,
			"protocol", "lock-denied", NULL,
			NULL, "Access to the requested lock is denied because the lock is currently held by another entity.", 0);
	} else if (strstr(msg, "to be created already exists."))
		srplg_errinfo_set_netconf_error(net_err,
			"protocol", "data-exists", NULL,
			NULL, msg, 0);
	else if (strstr(msg, "does not exist."))
		srplg_errinfo_set_netconf_error(net_err,
			"protocol", "data-missing", NULL,
			NULL, msg, 0);
	else
		srplg_errinfo_set_netconf_error(net_err,
			"application", "operation-failed", NULL,
			NULL, msg, 0);

	free(path);
	free(str);
	free(str2);
}


static void
rest_err_create(struct restconf_ctx        *ctx,
                const sr_error_info_err_t  *err,
                struct lyd_node           **root,
                char                      **tag,
                char                      **msg)
{
	const char *err_type;
	const char *err_tag;
	const char *err_app_tag;
	const char *err_path;
	const char *err_msg;
	const char **err_info_elem = NULL;
	const char **err_info_val = NULL;
	uint32_t err_info_count;
	const struct ly_ctx *ly_ctx;
	struct lyd_node *errors = NULL;
	struct lyd_node *error = NULL;
	struct lyd_node *info = NULL;
	const struct lys_module *nc_mod;
	sr_error_info_t *net_err = NULL;

	if (!err->error_format || strcmp(err->error_format, "NETCONF")) {
		sr_err_to_netconf_error(ctx, err, &net_err);
		err = &net_err->err[0];
	}

	sr_err_get_netconf_error(err,
	                         &err_type,
	                         &err_tag,
	                         &err_app_tag,
	                         &err_path,
	                         &err_msg,
	                         &err_info_elem,
	                         &err_info_val,
	                         &err_info_count);

	if (tag)
		*tag = strdup(err_tag);
	if (msg)
		*msg = strdup(err_msg);

	ly_ctx = sr_acquire_context(h2c_stream_get_conn(ctx->stream));
	nc_mod = ly_ctx_get_module_implemented(ly_ctx, "ietf-restconf");
	rest_assert(nc_mod);
	if (lyd_new_inner(NULL, nc_mod, "errors", 0, &errors))
		goto error;

	if (lyd_new_list(errors, NULL, "error", 0, &error))
		goto error;

	if (lyd_new_term(error, NULL, "error-type", err_type, 0, NULL))
		goto error;

	if (lyd_new_term(error, NULL, "error-tag", err_tag, 0, NULL))
		goto error;

	if (err_app_tag && err_app_tag[0]) {
		if (lyd_new_term(error, NULL, "error-app-tag", err_app_tag, 0, NULL))
			goto error;
	}

	if (err_path && err_path[0]) {
		if (lyd_new_term(error, NULL, "error-path", err_path, 0, NULL))
			goto error;
	}

	if (err_msg && err_msg[0]) {
		if (lyd_new_term(error, NULL, "error-message", err_msg, 0, NULL))
			goto error;
	}

	if (err_info_count) {
		if (lyd_new_inner(error, NULL, "error-info", 0, &info))
			goto error;

		for (uint32_t i = 0; i < err_info_count; i++) {
			if (lyd_new_term(error, NULL, err_info_elem[i], err_info_val[i], 0, NULL))
				goto error;
		}
	}

	lyd_merge_siblings(root, errors, 0);
error:
	srplg_errinfo_free(&net_err);
	free(err_info_elem);
	free(err_info_val);
	lyd_free_siblings(error);
	lyd_free_siblings(errors);
	sr_release_context(h2c_stream_get_conn(ctx->stream));
}

int
restconf_send_error(struct restconf_ctx *ctx, const sr_error_info_t *errors)
{
	LYD_FORMAT ly_fmt = restconf_get_format(ctx);
	uint32_t options = LYD_PRINT_SIBLINGS | LYD_PRINT_SHRINK;
	size_t nb = 2;
	char *err_tag = NULL;
	char *err_msg = NULL;
	struct lyd_node *root = NULL;
	nghttp2_nv hdrs[3] = {
		MAKE_NV_TEMP(":status"),
		MAKE_NV_TEMP("content-type"),
	};

	set_nv(&hdrs[1], ly_fmt == LYD_JSON ? CONTENT_JSON : CONTENT_XML);
	rest_err_create(ctx, &errors->err[0], &root, &err_tag, &err_msg);
	rest_assert(root);
	restconf_set_status(ctx, err_tag, err_msg, hdrs, &nb);
	free(err_tag);
	free(err_msg);
	for (size_t i = 1; i < errors->err_count; i++)
		rest_err_create(ctx, &errors->err[i], &root, NULL, NULL);

	rest_assert(!ctx->output);
	ctx->output = evbuffer_new();
	if (!ctx->output)
		return -ENOMEM;

	lyd_print_clb(restconf_write_cb, ctx, root, ly_fmt, options);
	lyd_free_siblings(root);
	return h2c_send_answer(ctx->stream, hdrs, nb, ctx->output);
}

int
restconf_send_answer(struct restconf_ctx *ctx, const struct lyd_node *root)
{
	LYD_FORMAT ly_fmt = restconf_get_format(ctx);
	uint32_t options = LYD_PRINT_SHRINK | LYD_PRINT_SIBLINGS;
	struct ly_set *set = NULL;
	int fixup_json = 0;
	const struct lyd_node *node;
	char length[64];
	nghttp2_nv hdrs[] = {
		MAKE_NV_OK,
		MAKE_NV_TEMP("content-type"),
		MAKE_NV_TEMP("content-length"),
	};

	node = root;
	if (node && ctx->xpath) {

		lyd_find_xpath(node, ctx->xpath, &set);
		if (!set)
			goto err;

		if ((ly_fmt == LYD_XML) && (set->count != 1)) {
			ly_set_free(set, NULL);
			goto err;
		}

		node = set->dnodes[0];
		fixup_json = (ly_fmt == LYD_JSON) &&
		             (set->count == 1) &&
		             (ctx->xpath[strlen(ctx->xpath) - 1] == ']');
	}

	set_nv(&hdrs[1], ly_fmt == LYD_JSON ? CONTENT_JSON : CONTENT_XML);
	rest_assert(!ctx->output);
	ctx->output = evbuffer_new();
	if (!ctx->output)
		return -ENOMEM;

	options |= ctx->lyd_options;
	lyd_print_clb(restconf_write_cb, ctx, node, ly_fmt, options);
	if (set)
		ly_set_free(set, NULL);

	if (fixup_json) {
		struct evbuffer_ptr first;
		struct evbuffer_ptr last;
		struct evbuffer_iovec iovec[1];

		first = evbuffer_search(ctx->output, "[", 1, NULL);
		evbuffer_peek(ctx->output, 1, &first, iovec, 1);
		((char *)(iovec[0].iov_base))[0] = ' ';

		while (first.pos != -1) {
			evbuffer_ptr_set(ctx->output, &first, 1, EVBUFFER_PTR_ADD);
			first = evbuffer_search(ctx->output, "]", 1, &first);
			if (first.pos != -1)
				last = first;

		}
		evbuffer_peek(ctx->output, 1, &last, iovec, 1);
		((char *)(iovec[0].iov_base))[0] = ' ';
	}

	snprintf(length, sizeof(length), "%zu", evbuffer_get_length(ctx->output));
	set_nv(&hdrs[2], length);
	return h2c_send_answer(ctx->stream, hdrs, stroll_array_nr(hdrs),
	                       ctx->method == METHOD_HEAD ? NULL : ctx->output);
err:
	srplg_errinfo_set_netconf_error(&ctx->errors, "protocol", "invalid-value", NULL, NULL, "More than one instance", 0);
	return restconf_send_error(ctx, ctx->errors);
}

int
restconf_not_content_answer(struct restconf_ctx *ctx) {
	nghttp2_nv hdrs[] = {
		MAKE_NV(":status", "204", 3),
	};

	return h2c_send_answer(ctx->stream, hdrs, stroll_array_nr(hdrs), NULL);
}