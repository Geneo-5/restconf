/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#include "restconf/sysrepo.h"
#include <errno.h>

struct plugin_ctx {
	sr_subscription_ctx_t *subscription;
};

#define XPATH_CAPABILITY "/ietf-restconf-monitoring:restconf-state/capabilities/capability"
#if defined(CONFIG_CAPABILITY_REPORT_ALL)
#define DEFAULTS_MODE "urn:ietf:params:restconf:capability:defaults:1.0?basic-mode=report-all"
#elif defined(CONFIG_CAPABILITY_TRIM)
#define DEFAULTS_MODE "urn:ietf:params:restconf:capability:defaults:1.0?basic-mode=trim"
#elif defined(CONFIG_CAPABILITY_EXPLICIT)
#define DEFAULTS_MODE "urn:ietf:params:restconf:capability:defaults:1.0?basic-mode=explicit"
#else
#error Invalid Capability defaults mode
#endif

static int
plugin_capabilities_cb(sr_session_ctx_t  *session __unused,
                   uint32_t           sub_id __unused,
                   const char        *module_name __unused,
                   const char        *path __unused,
                   const char        *request_xpath __unused,
                   uint32_t           operation_id __unused,
                   struct lyd_node  **parent,
                   void              *private_data __unused)
{
	const struct ly_ctx *ly_ctx;

	ly_ctx = sr_session_acquire_context(session);
	lyd_new_path(NULL, ly_ctx, XPATH_CAPABILITY, DEFAULTS_MODE, 0, parent);
	// lyd_print_fd(1, *parent, LYD_JSON, 0);
	sr_session_release_context(session);
	return SR_ERR_OK;
}

int
sr_plugin_init_cb(sr_session_ctx_t *session, void **private_data)
{

	struct plugin_ctx *ctx;

	*private_data = NULL;
	ctx = malloc(sizeof(*ctx));
	if (!ctx)
		return -ENOMEM;

	ctx->subscription = NULL;
	sr_oper_get_subscribe(session,
	                      "ietf-restconf-monitoring",
	                      "/ietf-restconf-monitoring:restconf-state/capabilities",
	                      plugin_capabilities_cb,
	                      ctx,
	                      0,
	                      &ctx->subscription);
	*private_data = ctx;
	return SR_ERR_OK;
}

void
sr_plugin_cleanup_cb(sr_session_ctx_t *session __unused, void *private_data)
{
	struct plugin_ctx *ctx = private_data;

	if (ctx && ctx->subscription)
		sr_unsubscribe_sub(ctx->subscription, 0);

	free(ctx);
}
