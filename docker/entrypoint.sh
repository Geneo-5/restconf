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

    echo ""
    echo "🏃 Exécution des tests..."
    echo ""
fi

if [[ " $@ " == *" --listen "* ]]; then
    sysrepo-plugind -d -v${SYSREPO_LOGLEVEL} &
    sleep 1
    exec /usr/local/sbin/restconfd
fi

# Exécuter pytest avec les tests
cd /workspace
exec pytest test/ $@
