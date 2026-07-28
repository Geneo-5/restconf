/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#ifndef _RESTCONF_CDEF_H
#define _RESTCONF_CDEF_H

#include "restconf/config.h"
#include <stroll/cdefs.h>

#if defined(CONFIG_RESTCONF_ASSERT)
#include <stroll/assert.h>
#define __rest_nonull(_arg_index, ...)
#define rest_assert(_expr) \
	stroll_assert("restconf", _expr)
#else /* !defined(CONFIG_RESTCONF_ASSERT) */
#define __rest_nonull(_arg_index, ...) \
	__nonull(_arg_index, ## __VA_ARGS__)
#define rest_assert(_expr)
#endif /* defined(CONFIG_RESTCONF_ASSERT) */

#define __aligned __attribute__ ((aligned))

#endif /* _RESTCONF_CDEF_H */

