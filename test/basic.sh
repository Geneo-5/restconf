#!/bin/bash

# Test Root Discovery
nghttp -v --no-tls http://127.0.0.1:8080/.well-known/host-meta

# Test API Resource (JSON)
nghttp -v --no-tls -H "Accept: application/yang-data+json" http://127.0.0.1:8080/restconf

# Test API Resource (XML)
nghttp -v --no-tls -H "Accept: application/yang-data+xml" http://127.0.0.1:8080/restconf