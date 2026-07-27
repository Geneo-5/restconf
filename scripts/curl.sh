#!/bin/sh -e

ARGS="-vvv --insecure --http2 --tlsv1.3"
ARG_JSON="${ARGS} -H 'Accept: application/yang-data+json'"
ARG_XML="${ARGS} -H 'Accept: application/yang-data+xml'"
URL="https://127.0.0.1:8080"

curl ${ARGS} ${URL}/.well-known/host-meta
curl ${ARGS} ${URL}/.well-known/host-meta.json
curl ${ARG_JSON} ${URL}/restconf