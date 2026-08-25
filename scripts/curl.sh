#!/bin/sh -e

ARGS="-f --insecure --http2 --tlsv1.3"

#test JWT none -> name = admin
JWT=eyJhbGciOiJub25lIn0.eyJuYW1lIjoiYWRtaW4ifQ.
AUTH_HEADER="Authorization: Bearer ${JWT}"

JSON="Accept: application/yang-data+json"
XML="Accept: application/yang-data+xml"
URL="https://127.0.0.1"

curl $ARGS $@ -H "$AUTH_HEADER"            ${URL}/.well-known/host-meta
echo
curl $ARGS $@ -H "$AUTH_HEADER"            ${URL}/.well-known/host-meta.json
echo
curl $ARGS $@ -H "$AUTH_HEADER" -H "$JSON" ${URL}/restconf
echo
curl $ARGS $@ -H "$AUTH_HEADER" -H "$JSON" ${URL}/restconf/ds
echo
curl $ARGS $@ -H "$AUTH_HEADER" -H "$JSON" ${URL}/restconf/operations
echo