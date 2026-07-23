################################################################################
# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN
################################################################################

common-cflags         := -Wall \
                         -Wextra \
                         -Wformat=2 \
                         -Wconversion \
                         -Wundef \
                         -Wshadow \
                         -Wcast-qual \
                         -Wcast-align \
                         -Wmissing-declarations \
                         -fvisibility=hidden \
                         -D_GNU_SOURCE \
                         -iquote $(TOPDIR)/include \
                         -I $(TOPDIR)/include \
                         $(EXTRA_CFLAGS)

ifneq ($(filter y,$(CONFIG_RESTCONF_ASSERT)),)
common-cflags         := $(filter-out -DNDEBUG,$(common-cflags))
endif # ($(filter y,$(CONFIG_RESTCONF_ASSERT)),)

common-ldflags        := $(common-cflags) \
                         $(EXTRA_LDFLAGS) \
                         -pthread -Wl,-z,start-stop-visibility=hidden

ifneq ($(filter y,$(CONFIG_RESTCONF_ASSERT)),)
common-ldflags        := $(filter-out -DNDEBUG,$(common-ldflags))
endif # ($(filter y,$(CONFIG_RESTCONF_ASSERT)),)

shared-common-cflags  := $(filter-out -fpie -fPIE,$(common-cflags)) -fpic

shared-common-ldflags := $(filter-out -pie -fpie -fPIE,$(common-ldflags)) \
                         -shared -Bsymbolic -fpic

common-pkgconf        := sysrepo libyang libevent
