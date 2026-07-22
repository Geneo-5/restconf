################################################################################
# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN
################################################################################

config-in                = Config.in
config-h                 = restconf/config.h

subdirs                  = src

################################################################################
# Source code tags generation
################################################################################

tagfiles := $(shell find $(CURDIR) -type f)
