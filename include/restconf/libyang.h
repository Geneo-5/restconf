/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#ifndef _RESTCONF_LIBYANG_H
#define _RESTCONF_LIBYANG_H

#include "restconf/cdef.h"
#include <sysrepo.h>
#include <event2/buffer.h>

LY_ERR
ly_in_new_evbuffer(struct evbuffer *body, struct ly_in **in)
	__rest_nonull(1, 2);

#endif /* _RESTCONF_LIBYANG_H */
