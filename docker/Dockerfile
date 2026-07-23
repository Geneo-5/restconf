################################################################################
# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN
################################################################################

FROM debian:trixie-slim
ENV DEBIAN_FRONTEND=noninteractive

# ==============================================================================
# Flags C/C++ globaux pour la sécurité
# ==============================================================================
ENV CFLAGS="-Wall -Wextra -Wmissing-declarations -Wstrict-prototypes \
	-Wshadow -Wformat=2 -Wuninitialized \
	-fstack-protector-strong -D_FORTIFY_SOURCE=2 \
	-O2 -ggdb3"
ENV CXXFLAGS="${CFLAGS}"
ENV LDFLAGS="-Wl,-z,relro,-z,now"

# ==============================================================================
# Installation des outils de compilation et dépendances système de base
# ==============================================================================
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    wget \
    ca-certificates \
    pkg-config \
    libpcre2-dev \
    zlib1g-dev \
    libssl-dev \
    python3-pip \
    libjson-c-dev \
    python3 \
    strace \
    gdb \
    valgrind \
    && rm -rf /var/lib/apt/lists/*

# ==============================================================================
# Répertoire de travail temporaire pour la compilation des dépendances
# ==============================================================================
WORKDIR /tmp/deps


ARG LIBYANG_SRC=https://github.com/CESNET/libyang/archive/refs/tags/v5.8.6.tar.gz
ARG SYSREPO_SRC=https://github.com/sysrepo/sysrepo/archive/refs/tags/v5.1.0.tar.gz
ARG LIBEVENT_SRC=https://github.com/libevent/libevent/releases/download/release-2.1.13-stable/libevent-2.1.13-stable.tar.gz
ARG NGHTTP2_SRC=https://github.com/nghttp2/nghttp2/releases/download/v1.69.0/nghttp2-1.69.0.tar.gz

# ----------------------------------------------------------------------------
# 1. Compilation de libyang
# ----------------------------------------------------------------------------
RUN echo "🔨 Compilation de libyang..." && \
    wget -qO- ${LIBYANG_SRC} | tar xz && \
    cd libyang-* && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX=/usr \
          -DBUILD_TESTING=OFF \
          -DENABLE_STATIC=OFF \
          -DENABLE_SHARED=ON \
          .. && \
    make -j$(nproc) && \
    make install && \
    ldconfig

# ----------------------------------------------------------------------------
# 2. Compilation de sysrepo
# ----------------------------------------------------------------------------
RUN echo "🔨 Compilation de sysrepo..." && \
    wget -qO- ${SYSREPO_SRC} | tar xz && \
    cd sysrepo-* && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX=/usr \
          -DBUILD_TESTING=OFF \
          -DNOTIFD_SETUP=ON \
          .. && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    sysrepoctl -i ../examples/plugin/oven.yang && \
    sysrepo-plugind -P examples/oven.so

# ----------------------------------------------------------------------------
# 3. Compilation de libevent
# ----------------------------------------------------------------------------
RUN echo "🔨 Compilation de libevent..." && \
    wget -qO- ${LIBEVENT_SRC} | tar xz && \
    cd libevent-* && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX=/usr \
          -DBUILD_TESTING=OFF \
          .. && \
    make -j$(nproc) && \
    make install && \
    ldconfig

# ----------------------------------------------------------------------------
# 4. Compilation de nghttp2
# ----------------------------------------------------------------------------
RUN echo "🔨 Compilation de nghttp2..." && \
    wget -qO- ${NGHTTP2_SRC} | tar xz && \
    cd nghttp2-* && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX=/usr \
          -DENABLE_APP=OFF \
          -DENABLE_HPACK=OFF \
          -DBUILD_TESTING=OFF \
          .. && \
    make -j$(nproc) && \
    make install && \
    ldconfig

# Nettoyage des sources compilées pour réduire la taille de l'image
RUN rm -rf /tmp/deps

