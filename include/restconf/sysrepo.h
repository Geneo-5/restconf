/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#ifndef _RESTCONF_SYSREPO_H
#define _RESTCONF_SYSREPO_H

#include "restconf/cdef.h"
#include <sysrepo.h>

extern int
sr_plugin_init_cb(sr_session_ctx_t *session, void **private_data)
	__restconf_nonull(1, 2);

extern void
sr_plugin_cleanup_cb(sr_session_ctx_t *session, void *private_data)
	__restconf_nonull(1, 2);

#endif /* _RESTCONF_SYSREPO_H */
