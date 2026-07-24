/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#include "restconf/sysrepo.h"

int
sr_plugin_init_cb(sr_session_ctx_t *session __unused, void **private_data __unused)
{
	return 0;
}

void
sr_plugin_cleanup_cb(sr_session_ctx_t *session __unused, void *private_data __unused)
{

}
