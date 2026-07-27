#!/bin/sh -e

# Options par défaut
USE_CACHE=true
TAG="restconfd:test-plugin"
DOCKER_FILE="docker/Dockerfile"

# Mode de compilation (par défaut: avec JWT insecure, sans plugin externe)
JWT_MODE="ON"
PYTEST_OPT="--show-capture no"
LOG_LEVEL=2
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
        --listen)
            PORT="-it -p 8080:8080"
            PYTEST_OPT="--listen"
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
            echo "  --verbose[=LV] set verbose mode (0=TRACE 5=EMERGENCY)"
            echo ""
            #echo "PYTEST_OPTION:  Forward to pythtest"
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


echo "🚀 Démarrage du build Docker..."
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
    -e SYSREPO_LOGLEVEL=${LOG_LEVEL} \
    ${PORT} \
    ${TAG} \
    ${PYTEST_OPT} $@
