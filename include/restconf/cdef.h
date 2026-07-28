/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#ifndef _RESTCONF_CDEF_H
#define _RESTCONF_CDEF_H

/**
 * @file
 * @brief Common compile-time definitions and helper macros.
 *
 * This header centralizes small build-configuration dependent helpers used
 * throughout the restconf codebase:
 *  - the #rest_assert() internal assertion macro, toggled by the
 *    @c CONFIG_RESTCONF_ASSERT Kconfig option ;
 *  - the #__rest_nonull attribute helper, used to annotate function
 *    arguments that must never be @c NULL ;
 *  - the #__aligned attribute helper.
 *
 * @note When @c CONFIG_RESTCONF_ASSERT is enabled, #rest_assert() performs a
 *       real runtime check (via stroll_assert()) and #__rest_nonull is
 *       disabled, since the corresponding argument is already validated at
 *       runtime by the assertion. When disabled, the opposite trade-off is
 *       made: #rest_assert() compiles to nothing (zero runtime cost) and
 *       #__rest_nonull enables the compiler's static "nonnull" checking
 *       instead.
 */

#include "restconf/config.h"
#include <stroll/cdefs.h>

#if defined(CONFIG_RESTCONF_ASSERT)
#include <stroll/assert.h>
/**
 * @def __rest_nonull
 * @brief Mark the given function argument(s) as never @c NULL.
 *
 * @param _arg_index one or more 1-based argument indexes to annotate as
 *                    non-null.
 *
 * Expands to the compiler's @c nonnull attribute when
 * @c CONFIG_RESTCONF_ASSERT is disabled. When @c CONFIG_RESTCONF_ASSERT is
 * enabled, expands to nothing, since #rest_assert() is expected to perform
 * the corresponding runtime check instead.
 */
#define __rest_nonull(_arg_index, ...)
/**
 * @def rest_assert
 * @brief Internal consistency assertion.
 *
 * @param _expr boolean expression that must hold true.
 *
 * Wraps stroll_assert() under the @c "restconf" assertion domain. Only
 * active when @c CONFIG_RESTCONF_ASSERT is enabled ; expands to nothing
 * otherwise, so @p _expr is never evaluated and incurs no runtime cost.
 */
#define rest_assert(_expr) \
	stroll_assert("restconf", _expr)
#else /* !defined(CONFIG_RESTCONF_ASSERT) */
#define __rest_nonull(_arg_index, ...) \
	__nonull(_arg_index, ## __VA_ARGS__)
#define rest_assert(_expr)
#endif /* defined(CONFIG_RESTCONF_ASSERT) */

/**
 * @def __aligned
 * @brief Request naturally-strictest alignment for the tagged declaration.
 *
 * Shorthand for @c __attribute__((aligned)), letting the compiler pick the
 * strictest alignment suitable for the target type.
 */
#define __aligned __attribute__ ((aligned))

#endif /* _RESTCONF_CDEF_H */

