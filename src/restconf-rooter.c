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
restconf_parse_path(struct restconf_ctx *ctx, const char *path)
{
	ctx->datastore = SR_DS_RUNNING;

	if (strcmp(path, "/restconf") != 0)
		goto err;

	ctx->xpath = strdup("/*");
	return 0;

err:
	srplg_errinfo_set_netconf_error(&ctx->errors, "protocol", "invalid-value",
		NULL, NULL, "Invalid path", 0);
	return 0;
}
