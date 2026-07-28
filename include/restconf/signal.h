/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#ifndef _RESTCONF_SIGNAL_H
#define _RESTCONF_SIGNAL_H

/**
 * @file
 * @brief Signal handling integration with the libevent event loop.
 *
 * Provides a small helper object that captures the process signals relevant
 * to the RESTCONF daemon lifecycle (termination requests) via @c signalfd(2)
 * and dispatches them through the libevent event loop, so that the daemon
 * can shut down cleanly instead of being killed abruptly.
 */

#include "restconf/cdef.h"
#include <event2/event.h>

/**
 * @brief Opaque signal handling context.
 *
 * Created by create_signal() and destroyed by destroy_signal(). Holds the
 * @c signalfd(2) file descriptor and the associated libevent event used to
 * watch it.
 */
struct rest_signal;

/**
 * @brief Create and register a signal handling context.
 *
 * Blocks the process signals of interest (@c SIGHUP, @c SIGINT, @c SIGQUIT,
 * @c SIGTERM, @c SIGUSR1, @c SIGUSR2), opens a @c signalfd(2) watching them,
 * and registers a persistent read event on @p base to dispatch them.
 *
 * @c SIGHUP, @c SIGINT, @c SIGQUIT and @c SIGTERM trigger a clean shutdown
 * of the event loop (event_base_loopbreak()). @c SIGUSR1 and @c SIGUSR2 are
 * currently ignored.
 *
 * @param base libevent event base the signal watcher event is registered
 *             onto. Must not be @c NULL.
 *
 * @return a newly allocated #rest_signal context on success, or @c NULL on
 *         failure (with @c errno set accordingly).
 *
 * @see destroy_signal()
 */
struct rest_signal *
create_signal(struct event_base *base)
	__rest_nonull(1);

/**
 * @brief Release a signal handling context.
 *
 * Unregisters and frees the libevent watcher event and closes the
 * underlying @c signalfd(2) descriptor.
 *
 * @param signal context returned by create_signal(), or @c NULL (no-op).
 */
void
destroy_signal(struct rest_signal *signal);

#endif /* _RESTCONF_SIGANL_H */

