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
ENV CFLAGS="-Wall -Wextra -Wshadow -Wformat=2 -Wuninitialized \
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
    unzip \
    kconfig-frontends \
    haproxy \
    libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

# ==============================================================================
# Répertoire de travail temporaire pour la compilation des dépendances
# ==============================================================================
WORKDIR /tmp/deps

# ----------------------------------------------------------------------------
# 1. Compilation de libyang
# ----------------------------------------------------------------------------
ARG LIBYANG_SRC=https://github.com/CESNET/libyang/archive/refs/tags/v5.8.6.tar.gz
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
ARG SYSREPO_SRC=https://github.com/sysrepo/sysrepo/archive/refs/tags/v5.1.0.tar.gz
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
ARG LIBEVENT_SRC=https://github.com/libevent/libevent/releases/download/release-2.1.13-stable/libevent-2.1.13-stable.tar.gz
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
ARG NGHTTP2_SRC=https://github.com/nghttp2/nghttp2/releases/download/v1.69.0/nghttp2-1.69.0.tar.gz
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

# ----------------------------------------------------------------------------
# 4. Compilation de libjwt
# ----------------------------------------------------------------------------
ARG LIBJWT_SRC=https://github.com/benmcollins/libjwt/releases/download/v3.6.1/libjwt-3.6.1.tar.xz
RUN echo "🔨 Compilation de libjwt..." && \
    wget -qO- ${LIBJWT_SRC} | tar xJ && \
    cd libjwt-* && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX=/usr \
          -DWITH_JSON_C=ON \
          -DWITH_OPENSSL=ON \
          -DWITH_GNUTLS=OFF \
          -DWITH_MBEDTLS=OFF \
          -DWITH_LIBCURL=OFF \
          -DWITH_TESTS=OFF \
          -DWITH_KCAPI_MD=OFF \
          -DENABLE_COVERAGE=OFF \
          -DEXCLUDE_DEPRECATED=OFF \
          -DWITH_ML_DSA=OFF \
          .. && \
    make -j$(nproc) && \
    make install && \
    ldconfig

# ----------------------------------------------------------------------------
# 5. Install ebuild
# ----------------------------------------------------------------------------
ARG EBUILD_SRC=https://github.com/grgbr/ebuild/archive/refs/heads/master.zip
RUN echo "🔨 Install eBuild..." && \
    wget -q ${EBUILD_SRC} && unzip master && rm master.zip && \
    cd ebuild-* && \
    make install DATADIR=/usr/share && \
    chmod +x /usr/share/ebuild/scripts/*

# ----------------------------------------------------------------------------
# 6. Compilation de stroll
# ----------------------------------------------------------------------------
ARG STROLL_SRC=https://github.com/grgbr/stroll/archive/refs/heads/master.zip
RUN echo "🔨 Compilation de stroll..." && \
    wget -q ${STROLL_SRC} && unzip master && rm master.zip && \
    cd stroll-* && \
    make defconfig && \
    make PREFIX=/usr -j$(nproc) && \
    make PREFIX=/usr install && \
    ldconfig

# ----------------------------------------------------------------------------
# 7. Compilation de utils
# ----------------------------------------------------------------------------
ARG UTILS_SRC=https://github.com/grgbr/utils/archive/refs/heads/master.zip
RUN echo "🔨 Compilation de utils..." && \
    wget -q ${UTILS_SRC} && unzip master && rm master.zip && \
    cd utils-* && \
    make defconfig && \
    make PREFIX=/usr -j$(nproc) && \
    make PREFIX=/usr install && \
    ldconfig

# Nettoyage des sources compilées pour réduire la taille de l'image
RUN rm -rf /tmp/deps

# ----------------------------------------------------------------------------
# Install and check HAProxy config
# ----------------------------------------------------------------------------
COPY docker/haproxy.cfg /etc/haproxy/haproxy.cfg
COPY docker/dev.pem /etc/haproxy/certs/restconf.pem
RUN haproxy -c -V -f /etc/haproxy/haproxy.cfg

# ==============================================================================
# Compilation du projet RESTCONF principal
# ==============================================================================
WORKDIR /workspace
COPY . .

# Installer les dépendances Python pour les tests
#RUN pip install --break-system-packages -r test/requirements.txt
ENV PKG_CONFIG_PATH=/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/lib/pkgconfig
ENV EXTRA_CFLAGS="-Werror ${CFLAGS} -Wmissing-declarations -Wstrict-prototypes \
                 -fsanitize=address -fsanitize=pointer-compare \
                 -fsanitize=pointer-subtract -fsanitize=leak \
                 -fsanitize=undefined"
ENV EXTRA_LDFLAGS="${LDFLAGS}"

# Compilation du projet RESTCONF avec les options configurables
RUN echo "🔨 Compilation du projet RESTCONF..." && \
    make -j$(nproc) \
         EXTRA_CFLAGS="${EXTRA_CFLAGS}" \
         EXTRA_LDFLAGS="${EXTRA_LDFLAGS}" && \
    make install


# Copier le script d'entrée
COPY docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

# Configurer le workspace comme répertoire de travail
WORKDIR /workspace

# Message de succès
ENTRYPOINT ["/entrypoint.sh"]
