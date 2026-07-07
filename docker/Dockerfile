# ==============================================================================
# Dockerfile : Build des dépendances et du projet RESTCONF sur Debian
# ==============================================================================
FROM debian:bookworm-slim

# Éviter les invites interactives (tzdata, etc.)
ENV DEBIAN_FRONTEND=noninteractive

# Installation des outils de compilation et dépendances système de base
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
    && rm -rf /var/lib/apt/lists/*

# Répertoire de travail temporaire pour la compilation des dépendances
WORKDIR /tmp/deps

# ----------------------------------------------------------------------------
# 1. Compilation de libyang (v5.8.6)
# ----------------------------------------------------------------------------
RUN echo "🔨 Compilation de libyang..." && \
    wget -qO- https://github.com/CESNET/libyang/archive/refs/tags/v5.8.6.tar.gz | tar xz && \
    cd libyang-* && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=OFF .. && \
    make -j$(nproc) && \
    make install

# ----------------------------------------------------------------------------
# 2. Compilation de sysrepo (v5.1.0)
# ----------------------------------------------------------------------------
RUN echo "🔨 Compilation de sysrepo..." && \
    wget -qO- https://github.com/sysrepo/sysrepo/archive/refs/tags/v5.1.0.tar.gz | tar xz && \
    cd sysrepo-* && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=OFF .. && \
    make -j$(nproc) && \
    make install

# ----------------------------------------------------------------------------
# 3. Compilation de libevent (2.1.13-stable)
# ----------------------------------------------------------------------------
RUN echo "🔨 Compilation de libevent..." && \
    wget -qO- https://github.com/libevent/libevent/releases/download/release-2.1.13-stable/libevent-2.1.13-stable.tar.gz | tar xz && \
    cd libevent-* && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=OFF .. && \
    make -j$(nproc) && \
    make install

# ----------------------------------------------------------------------------
# 4. Compilation de nghttp2 (v1.69.0)
# ----------------------------------------------------------------------------
RUN echo "🔨 Compilation de nghttp2..." && \
    wget -qO- https://github.com/nghttp2/nghttp2/releases/download/v1.69.0/nghttp2-1.69.0.tar.gz | tar xz && \
    cd nghttp2-* && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DENABLE_APP=OFF -DENABLE_HPACK=OFF -DBUILD_TESTING=OFF .. && \
    make -j$(nproc) && \
    make install

# Mise à jour du cache des bibliothèques dynamiques pour que le linker les trouve
RUN ldconfig

# Nettoyage des sources compilées pour réduire la taille de l'image
WORKDIR /
RUN rm -rf /tmp/deps

# ----------------------------------------------------------------------------
# Compilation du projet RESTCONF principal
# ----------------------------------------------------------------------------
WORKDIR /workspace
COPY . .

RUN pip install --break-system-packages -r test/requirements.txt

RUN echo "🔨 Compilation du projet RESTCONF..." && \
    mkdir -p build && cd build && \
    cmake .. && \
    make -j$(nproc)

RUN echo "🔨 Compilation du projet RESTCONF + PLUGIN..." && \
    mkdir -p build_plugin && cd build_plugin && \
    cmake -DBUILD_EXTERNAL_PLUGIN=ON .. && \
    make -j$(nproc)

RUN echo "🔨 Compilation du projet RESTCONF..." && \
    mkdir -p build_test && cd build_test && \
    cmake -DALLOW_INSECURE_JWT=ON -DLOG_LEVEL=0 .. && \
    make -j$(nproc) && \
    make install

# Message de succès
CMD ["pytest", "test/", "-v"]