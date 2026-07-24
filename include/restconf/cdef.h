/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#ifndef _RESTCONF_CDEF_H
#define _RESTCONF_CDEF_H

#include <sys/cdefs.h>
#include "restconf/config.h"

/**
 * Tell compiler that a function, variable, type or (goto) label may possibly
 * be unused.
 *
 * May be used to prevent compiler from warning about unused functions,
 * parameters, variables, etc...
 *
 * @see
 * - [GCC common function attributes](https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#Common-Function-Attributes)
 * - [GCC common variable attributes](https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html#Common-Variable-Attributes)
 * - [GCC common type attributes](https://gcc.gnu.org/onlinedocs/gcc/Common-Type-Attributes.html#Common-Type-Attributes)
 * - [GCC label attributes](https://gcc.gnu.org/onlinedocs/gcc/Label-Attributes.html#Label-Attributes)
 */
#define __unused __attribute__((unused))

/**
 * @internal
 *
 * Declare to the compiler that a restconf function argument should be a
 * non-null pointer.
 *
 * @note
 * When compiled with the #CONFIG_RESTCONF_ASSERT build option enabled, this
 * macro expands as empty.
 */
#if defined(CONFIG_RESTCONF_ASSERT)

#define __restconf_nonull(_arg_index, ...)

#else  /* !(defined(CONFIG_RESTCONF_ASSERT) */

#define __restconf_nonull(_arg_index, ...) __attribute__((nonnull(_arg_index, ## __VA_ARGS__)))

#endif /* defined(CONFIG_RESTCONF_ASSERT) */

#endif /* _RESTCONF_CDEF_H */

