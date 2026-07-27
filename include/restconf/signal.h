/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#ifndef _RESTCONF_SIGNAL_H
#define _RESTCONF_SIGNAL_H

#include "restconf/cdef.h"
#include <event2/event.h>

struct rest_signal;

struct rest_signal *
create_signal(struct event_base *base)
	__rest_nonull(1);

void
destroy_signal(struct rest_signal *signal);

#endif /* _RESTCONF_SIGANL_H */

