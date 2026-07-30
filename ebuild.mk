################################################################################
# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN
################################################################################

config-in                = Config.in
config-h                 = restconf/config.h

subdirs                  = src
subdirs                 += $(call kconf_enabled,RESTCONF_TEST,test)

################################################################################
# Source code tags generation
################################################################################

yangs = ietf-restconf-monitoring@2017-01-26.yang \
        ietf-restconf-subscribed-notifications@2019-11-17.yang \
	ietf-restconf@2017-01-26.yang

install: $(addprefix $(CONFIG_YANG_PATH)/,$(yangs))

$(CONFIG_YANG_PATH)/%: yang/%
	$(call install_recipe,-m644,$(<),$(@))

tagfiles := $(shell find $(CURDIR) -type f)
