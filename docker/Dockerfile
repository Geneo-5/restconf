# ==============================================================================
# Dockerfile : Build des dépendances et du projet RESTCONF sur Debian
# ==============================================================================
#
# UTILISATION DU CACHE:
#   - La première compilation (docker build) va télécharger et compiler toutes les dépendances
#   - Les compilations suivantes utiliseront automatiquement le cache Docker
#   - Pour forcer une recompilation complète: docker build --no-cache
#   - Pour purger le cache: docker system prune -a
#
# OPTIONS DE COMPILATION (via --build-arg):
#   - ALLOW_INSECURE_JWT=ON|OFF  (désactive la vérification JWT, default: ON)
#   - BUILD_EXTERNAL_PLUGIN=ON|OFF (plugin externe vs intégré, default: OFF)
#   BUILD_TEST_PLUGIN est TOUJOURS ON dans ce Dockerfile
#
# Exemples:
#   docker build -f docker/Dockerfile -t restconf:test .
#   docker build --no-cache -f docker/Dockerfile -t restconf:test .
#   docker build --build-arg ALLOW_INSECURE_JWT=OFF -t restconf:jwt-secure .
#   docker build --build-arg BUILD_EXTERNAL_PLUGIN=ON -t restconf:external-plugin .
#
# ==============================================================================
FROM debian:bookworm-slim

# Éviter les invites interactives (tzdata, etc.)
ENV DEBIAN_FRONTEND=noninteractive

# ==============================================================================
# Flags C/C++ globaux pour la sécurité
# ==============================================================================
ENV CFLAGS="-Wall -Wextra -Wmissing-declarations -Wstrict-prototypes \
	-Wshadow -Wformat=2 -Wuninitialized \
	-fstack-protector-strong -D_FORTIFY_SOURCE=2 \
	-O2"
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
    && rm -rf /var/lib/apt/lists/*

# ==============================================================================
# Répertoire de travail temporaire pour la compilation des dépendances
# ==============================================================================
WORKDIR /tmp/deps

# ----------------------------------------------------------------------------
# 1. Compilation de libyang (v5.8.6)
# ----------------------------------------------------------------------------
RUN echo "🔨 Compilation de libyang v5.8.6..." && \
    wget -qO- https://github.com/CESNET/libyang/archive/refs/tags/v5.8.6.tar.gz | tar xz && \
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
# 2. Compilation de sysrepo (v5.1.0)
# ----------------------------------------------------------------------------
RUN echo "🔨 Compilation de sysrepo v5.1.0..." && \
    wget -qO- https://github.com/sysrepo/sysrepo/archive/refs/tags/v5.1.0.tar.gz | tar xz && \
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
# 3. Compilation de libevent (2.1.13-stable)
# ----------------------------------------------------------------------------
RUN echo "🔨 Compilation de libevent 2.1.13-stable..." && \
    wget -qO- https://github.com/libevent/libevent/releases/download/release-2.1.13-stable/libevent-2.1.13-stable.tar.gz | tar xz && \
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
# 4. Compilation de nghttp2 (v1.69.0)
# ----------------------------------------------------------------------------
RUN echo "🔨 Compilation de nghttp2 v1.69.0..." && \
    wget -qO- https://github.com/nghttp2/nghttp2/releases/download/v1.69.0/nghttp2-1.69.0.tar.gz | tar xz && \
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

# ==============================================================================
# Options de compilation du projet RESTCONF (configurables via docker build --build-arg)
# ==============================================================================
# Mode JWT: ON = désactive la vérification des signatures (DEBUG ONLY)
ARG ALLOW_INSECURE_JWT=ON
ENV ALLOW_INSECURE_JWT=${ALLOW_INSECURE_JWT}

# Mode Plugin: OFF = plugin intégré, ON = plugin externe (séparation des privilèges)
ARG BUILD_EXTERNAL_PLUGIN=OFF
ENV BUILD_EXTERNAL_PLUGIN=${BUILD_EXTERNAL_PLUGIN}

# Plugin de test: TOUJOURS ON dans Docker pour les tests de qualification
ENV BUILD_TEST_PLUGIN=ON

# Niveau de log: 0=TRACE, 1=DEBUG, 2=INFO, 3=WARN, 4=ERROR, 5=FATAL
ARG LOG_LEVEL=2
ENV LOG_LEVEL=${LOG_LEVEL}

# ==============================================================================
# Compilation du projet RESTCONF principal
# ==============================================================================
WORKDIR /workspace
COPY . .

# Installer les dépendances Python pour les tests
RUN pip install --break-system-packages -r test/requirements.txt

# Compilation du projet RESTCONF avec les options configurables
RUN echo "🔨 Compilation du projet RESTCONF..." && \
    echo "   JWT Insecure: ${ALLOW_INSECURE_JWT}" && \
    echo "   External Plugin: ${BUILD_EXTERNAL_PLUGIN}" && \
    echo "   Test Plugin: ${BUILD_TEST_PLUGIN}" && \
    mkdir -p build && cd build && \
    cmake -DALLOW_INSECURE_JWT=${ALLOW_INSECURE_JWT} \
          -DBUILD_EXTERNAL_PLUGIN=${BUILD_EXTERNAL_PLUGIN} \
          -DBUILD_TEST_PLUGIN=${BUILD_TEST_PLUGIN} \
          -DLOG_LEVEL=${LOG_LEVEL} \
          .. && \
    make -j$(nproc) && \
    make install && \
    make test-install

# ==============================================================================
# Installation des modules YANG et plugins pour les tests
# ==============================================================================
RUN sysrepoctl -i modules/ietf-restconf@2017-01-26.yang
RUN sysrepoctl -i modules/ietf-restconf-monitoring@2017-01-26.yang
RUN sysrepoctl -i modules/ietf-restconf-subscribed-notifications@2019-11-17.yang

# Vérification finale
RUN echo "📋 Vérification des modules et plugins:" && \
    sysrepoctl -l | grep -E "(oven|restconf-test)" || echo "❌ Modules non installés" && \
    echo "✅ Installation des dépendances terminée"

# Copier le script d'entrée
COPY docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

# Configurer le workspace comme répertoire de travail
WORKDIR /workspace

# Message de succès
ENTRYPOINT ["/entrypoint.sh"]
