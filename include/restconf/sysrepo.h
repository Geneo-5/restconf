/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#ifndef _RESTCONF_SYSREPO_H
#define _RESTCONF_SYSREPO_H

/**
 * @file
 * @brief Sysrepo plugin entry points.
 *
 * Declares the standard sysrepo plugin callbacks (@c sr_plugin_init_cb() /
 * @c sr_plugin_cleanup_cb()) implemented by restconf. These are the same
 * entry points sysrepo-plugind expects from a shared-library plugin ; when
 * @c CONFIG_SYSREPO_BUILTIN is selected, they are instead invoked directly
 * by the restconfd daemon itself during startup/shutdown (see main.c).
 */

#include "restconf/cdef.h"
#include <sysrepo.h>

/**
 * @brief Initialize the restconf sysrepo plugin.
 *
 * Called once, at plugin/daemon startup, with an already established
 * sysrepo session. Meant to perform any one-time setup required by the
 * plugin (e.g. subscriptions, private state allocation) and hand back an
 * opaque private context via @p private_data, which will be passed back
 * unchanged to sr_plugin_cleanup_cb().
 *
 * @param session      active sysrepo session usable to perform the plugin
 *                     initialization. Must not be @c NULL.
 * @param[out] private_data location where the plugin stores its private,
 *                          implementation-defined context. Must not be
 *                          @c NULL.
 *
 * @return @c 0 (::SR_ERR_OK) on success, or a sysrepo error code
 *         (@c sr_error_t) on failure.
 *
 * @see sr_plugin_cleanup_cb()
 */
extern int
sr_plugin_init_cb(sr_session_ctx_t *session, void **private_data)
	__rest_nonull(1, 2) __export_public;

/**
 * @brief Tear down the restconf sysrepo plugin.
 *
 * Called once, at plugin/daemon shutdown, to release whatever resources
 * were allocated by sr_plugin_init_cb().
 *
 * @param session      sysrepo session the plugin was initialized with.
 *                     Must not be @c NULL.
 * @param private_data private context previously returned by
 *                     sr_plugin_init_cb() via its @c private_data output
 *                     parameter. Must not be @c NULL.
 *
 * @see sr_plugin_init_cb()
 */
extern void
sr_plugin_cleanup_cb(sr_session_ctx_t *session, void *private_data)
	__rest_nonull(1, 2) __export_public;

#endif /* _RESTCONF_SYSREPO_H */
