################################################################################
# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN
################################################################################

include $(TOPDIR)/common.mk

subdirs            = sysrepo

bins              := restconfd
restconfd-objs    := main.o h2c_server.o signal.o well-known.o
restconfd-lots    := $(call kconf_enabled,SYSREPO_BUILTIN,sysrepo/builtin.a)
restconfd-cflags  := $(common-cflags)
restconfd-ldflags := $(common-ldflags)
restconfd-pkgconf := $(common-pkgconf) libevent libnghttp2 libcurl
restconfd-path    := $(SBINDIR)/restconfd

