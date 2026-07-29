################################################################################
# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN
################################################################################

include $(TOPDIR)/common.mk

solibs                        := restconf-test.so
restconf-test.o-objs          := $(plugin-objs)
restconf-test.o-cflags        := $(shared-common-cflags)
restconf-test.o-ldflags       := $(shared-common-ldflags)
restconf-test.o-pkgconf       := $(common-pkgconf)
restconf-test.o-path          := $(CONFIG_SYSREPO_PLUGIND_PATH)/restconf-test.so

install: install-yang

# ----------------------------------------------------------------------------
# Installation du module de qualification RESTCONF (tests §4+)
#   Le module 'restconf-test' et son jeu de données 'restconf-test.json' sont
#   requis par test/test_04_datastore_get.py (containers, listes, leaf-lists,
#   clés de liste spéciales). Le module 'oven', lui, est déjà installé à
#   l'étape 2 (compilation sysrepo) et couvert par test/test_05_oven.py.
# ----------------------------------------------------------------------------
.PHONY: install-yang
install-yang: restconf-test.yang restconf-test.jsons
	$(@)sysrepoctl -i test/restconf-test.yang --enable-feature=advanced-monitoring \
	               --enable-feature=legacy-support --init-data test/restconf-test.json