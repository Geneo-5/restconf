#!/bin/bash -e

if [[ " $@ " == *" -vv "* ]]; then
    echo "📋 Liste des modules YANG chargés:"
    sysrepoctl -l || true

    echo ""
    echo "📋 Liste des plugins Datastore / Notification chargés:"
    sysrepoctl -L || true

    echo ""
    echo "📋 Liste des plugins chargés:"
    find / -type d -name "sysrepo-plugind" -exec ls -l {}/plugins \;
fi

sysrepo-plugind -d -v${SYSREPO_LOGLEVEL} &
haproxy -d -f /etc/haproxy/haproxy.cfg &
sleep 1

if [[ " $@ " == *" --listen "* ]]; then
    exec /usr/local/sbin/restconfd -u /run/restconf.socket
fi

/usr/local/sbin/restconfd -d -u /run/restconf.socket

if [[ " $@ " == *" -vv "* ]]; then
    echo ""
    echo "🏃 Exécution des tests..."
    echo ""
fi

# Exécuter pytest avec les tests
cd /workspace
exec pytest test/ $@
