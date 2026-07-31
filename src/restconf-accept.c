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
restconf_parse_accept(struct restconf_ctx *ctx __unused, const char *accept __unused)
{
	return 0;
}

void
restconf_check_accept(struct restconf_ctx *ctx __unused)
{
}

LYD_FORMAT
restconf_get_format(struct restconf_ctx *ctx __unused)
{
	return LYD_JSON;
}
