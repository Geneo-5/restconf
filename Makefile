################################################################################
# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN
################################################################################

override PACKAGE := restconf
override VERSION := 1.0
EXTRA_CFLAGS     := -O2 -DNDEBUG -Wall -Wextra -Wformat=2
EXTRA_LDFLAGS     := -O2
includedir        := $(CURDIR)/include/restconf

export VERSION EXTRA_CFLAGS EXTRA_LDFLAGS

ifeq ($(strip $(EBUILDDIR)),)
ifneq ($(realpath res/ebuild/main.mk),)
EBUILDDIR := $(realpath res/ebuild)
else  # ($(realpath res/ebuild/main.mk),)
EBUILDDIR := $(realpath /usr/share/ebuild)
endif # !($(realpath res/ebuild/main.mk),)
endif # ($(strip $(EBUILDDIR)),)

ifeq ($(realpath $(EBUILDDIR)/main.mk),)
$(error '$(EBUILDDIR)': no valid eBuild install found !)
endif # ($(realpath $(EBUILDDIR)/main.mk),)

include $(EBUILDDIR)/main.mk
# ex: filetype=make :