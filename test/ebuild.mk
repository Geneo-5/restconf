################################################################################
# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN
################################################################################

include $(TOPDIR)/common.mk

solibs                     := restconf-test.so
restconf-test.so-objs      := restconf-test.o
restconf-test.so-cflags    := $(shared-common-cflags)
restconf-test.so-ldflags   := $(shared-common-ldflags)
restconf-test.so-pkgconf   := $(common-pkgconf)
restconf-test.so-path      := $(CONFIG_SYSREPO_PLUGIND_PATH)/restconf-test.so

install: $(CONFIG_YANG_PATH)/restconf-test.yang install-yang-test

$(CONFIG_YANG_PATH)/restconf-test.yang: restconf-test.yang
	$(call install_recipe,-m644,$(<),$(@))

# ----------------------------------------------------------------------------
# Installation du module de qualification RESTCONF (tests §4+)
#   Le module 'restconf-test' et son jeu de données 'restconf-test.json' sont
#   requis par test/test_04_datastore_get.py (containers, listes, leaf-lists,
#   clés de liste spéciales). Le module 'oven', lui, est déjà installé à
#   l'étape 2 (compilation sysrepo) et couvert par test/test_05_oven.py.
# ----------------------------------------------------------------------------
.PHONY: install-yang-test
install-yang-test: restconf-test.yang restconf-test.json
	$(Q)sysrepoctl -i restconf-test.yang --enable-feature=advanced-monitoring \
	               --enable-feature=legacy-support --init-data restconf-test.json