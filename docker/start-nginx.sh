#!/bin/sh

nginx -c /restconf/etc/nginx-restconf.conf.example &
exec restconfd :9000 1 /restconf