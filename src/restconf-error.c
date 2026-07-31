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
		set_nv(nv, "409");
		return;

	case 13:
		if (strcmp(tag, "invalid-value") == 0) {
			// 400, parfois 404 ou 406 selon contexte
			// Valeur invalide, ressource inexistante,
			// représentation invalide
			if (!ctx->xpath)
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
			make_nv(&nv[*nb++], WWW_AUTH, ctx->username ? WWW_AUTH_403 : WWW_AUTH_401);
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
			make_nv(&nv[*nb++], WWW_AUTH, WWW_AUTH_403);
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
		return;
	}

	rest_assert(0);
	set_nv(nv, "500");
}


static void
rest_err_create(struct restconf_ctx        *ctx,
                const sr_error_info_err_t  *err,
                struct lyd_node           **root,
                const char                **tag)
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
		*tag = err_tag;

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
	free(err_info_elem);
	free(err_info_val);
	lyd_free_siblings(error);
	lyd_free_siblings(errors);
	sr_release_context(h2c_stream_get_conn(ctx->stream));
}

int
restconf_send_error(struct restconf_ctx *ctx, sr_error_info_t *errors)
{
	LYD_FORMAT ly_fmt = restconf_get_format(ctx);
	uint32_t options = LYD_PRINT_SIBLINGS | LYD_PRINT_SHRINK;
	size_t nb = 2;
	const char *err_tag;
	struct lyd_node *root = NULL;
	nghttp2_nv hdrs[3] = {
		MAKE_NV_TEMP(":status"),
		MAKE_NV_TEMP("content-type"),
	};

	set_nv(&hdrs[1], ly_fmt == LYD_JSON ? CONTENT_JSON : CONTENT_XML);
	rest_err_create(ctx, &errors->err[0], &root, &err_tag);
	rest_assert(root);
	restconf_set_status(ctx, err_tag, hdrs, &nb);
	for (size_t i = 1; i < errors->err_count; i++)
		rest_err_create(ctx, &errors->err[i], &root, NULL);

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
	uint32_t options = LYD_PRINT_SIBLINGS | LYD_PRINT_SHRINK;
	nghttp2_nv hdrs[] = {
		MAKE_NV_OK,
	};

	rest_assert(!ctx->output);
	ctx->output = evbuffer_new();
	if (!ctx->output)
		return -ENOMEM;

	options |= LYD_PRINT_WD_ALL;
	lyd_print_clb(restconf_write_cb, ctx, root, ly_fmt, options);
	return h2c_send_answer(ctx->stream, hdrs, stroll_array_nr(hdrs), ctx->output);
}
