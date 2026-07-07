#!/bin/sh -ex

docker build . --file docker/Dockerfile --tag restconfd:latest
docker run --rm  restconfd:latest