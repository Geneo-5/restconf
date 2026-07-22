################################################################################
# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN
################################################################################

include $(TOPDIR)/common.mk

plugin-objs := plugin.o

solibs                   := $(call kconf_enabled,RESTCONF_SYSREPO_PLUGIND,restconf.so)
restconf.o-objs          := $(plugin-objs)
restconf.o-cflags        := $(shared-common-cflags)
restconf.o-ldflags       := $(shared-common-ldflags)
restconf.o-pkgconf       := $(common-pkgconf)
restconf.o-path          := $(CONFIG_SYSREPO_PLUGIND_PATH)/restconf.so

bins                     := $(call kconf_enabled,RESTCONF_SYSREPO_DAEMON,sysrepo-restconf)
sysrepo-restconf-objs    := main.o $(plugin-objs)
sysrepo-restconf-cflags  := $(common-cflags)
sysrepo-restconf-ldflags := $(common-ldflags)
sysrepo-restconf-pkgconf := $(common-pkgconf)
sysrepo-restconf-path    := $(SBINDIR)/sysrepo-restconf

builtins                 := $(call kconf_enabled,RESTCONF_SYSREPO_BUILTIN,builtin.a)
builtin.a-cflags         := $(common-cflags)
builtin.a-objs           := $(plugin-objs)
