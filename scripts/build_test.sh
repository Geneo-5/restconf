#!/bin/sh

# Script de build pour la compilation et l'exécution des tests RESTCONF
# Ce script utilise le cache Docker pour éviter de recompiler les dépendances

set -e

# Options par défaut
USE_CACHE=true
TAG="restconfd:test-plugin"
DOCKER_FILE="docker/Dockerfile"

# Mode de compilation (par défaut: avec JWT insecure, sans plugin externe)
JWT_MODE="ON"
PLUGIN_MODE="OFF"
PYTEST_OPT="--show-capture no"
LOG_LEVEL=5
PORT=

# Parser des arguments
while [ $# -gt 0 ]; do
    case "$1" in
        --no-cache)
            USE_CACHE=false
            shift
            ;;
        --tag=*)
            TAG="${1#*=}"
            shift
            ;;
        --jwt-on|--jwt-off)
            JWT_MODE="${1#--jwt-}"
            JWT_MODE="${JWT_MODE:-ON}"
            shift
            ;;
        --plugin-on|--plugin-off)
            PLUGIN_MODE="${1#--plugin-}"
            PLUGIN_MODE="${PLUGIN_MODE:-OFF}"
            shift
            ;;
        --verbose)
            PYTEST_OPT="-vv"
            LOG_LEVEL=0
            shift
            ;;
        --verbose=*)
            PYTEST_OPT="-vv"
            LOG_LEVEL=${1#*=}
            shift
            ;;
        --listen=*)
            PORT="-it -p ${1#*=}:8080"
            PYTEST_OPT="--listen"
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [options] [PYTEST_OPTION]"
            echo ""
            echo "Options:"
            echo "  --no-cache     Ne pas utiliser le cache Docker"
            echo "  --tag=NAME     Nom du tag Docker (default: restconfd:test-plugin)"
            echo "  --jwt-on       Compiler AVEC JWT insecure (default)"
            echo "  --jwt-off      Compiler SANS JWT insecure"
            echo "  --plugin-on    Compiler AVEC plugin externe"
            echo "  --plugin-off   Compiler SANS plugin externe (default)"
            echo "  --verbose[=LV] set verbose mode (0=TRACE 5=EMERGENCY)"
            echo ""
            echo "PYTEST_OPTION:  Forward to pythtest"
            echo ""
            echo "Exemples:"
            echo "  $0                               # Build avec cache, JWT ON, plugin OFF"
            echo "  $0 --no-cache --jwt-off          # Build sans cache, JWT OFF"
            echo "  $0 --no-cache --plugin-on        # Build sans cache, plugin ON"
            echo "  $0 --jwt-off --plugin-on         # Build avec cache, JWT OFF, plugin ON"
            exit 1
            ;;
        *)
            break;;
    esac
done

# Construire les arguments Docker
BUILD_ARGS="--file ${DOCKER_FILE} --tag ${TAG}"
if [ "$USE_CACHE" = false ]; then
    BUILD_ARGS="${BUILD_ARGS} --no-cache"
fi

# Passer les options de compilation au Dockerfile via --build-arg
BUILD_ARGS="${BUILD_ARGS} --build-arg ALLOW_INSECURE_JWT=${JWT_MODE}"
BUILD_ARGS="${BUILD_ARGS} --build-arg BUILD_EXTERNAL_PLUGIN=${PLUGIN_MODE}"
BUILD_ARGS="${BUILD_ARGS} --build-arg LOG_LEVEL=${LOG_LEVEL}"

echo "🚀 Démarrage du build Docker..."
echo "   Cache: ${USE_CACHE}"
echo "   JWT Insecure: ${JWT_MODE}"
echo "   External Plugin: ${PLUGIN_MODE}"
echo "   Tag: ${TAG}"
echo ""

# Construire l'image Docker
docker build ${BUILD_ARGS} .
docker image prune -f

echo ""
echo "✅ Image Docker construite avec succès"
echo ""

# Exécuter les tests
echo "🏃 Lancement des tests..."
echo ""
docker run --rm \
    --name restconf-test \
    -e ALLOW_INSECURE_JWT=${JWT_MODE} \
    -e BUILD_EXTERNAL_PLUGIN=${PLUGIN_MODE} \
    ${PORT} \
    ${TAG} \
    ${PYTEST_OPT} $@