################################################################################
# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN
################################################################################

include $(TOPDIR)/common.mk

subdirs            = sysrepo

bins              := restconfd
restconfd-objs    := main.o
restconfd-lots    := $(call kconf_enabled,RESTCONF_SYSREPO_BUILTIN,sysrepo/builtin.a)
restconfd-cflags  := $(common-cflags)
restconfd-ldflags := $(common-ldflags)
restconfd-pkgconf := $(common-pkgconf)
restconfd-path    := $(SBINDIR)/resconfd

