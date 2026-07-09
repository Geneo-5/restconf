#!/bin/sh -e

# Script d'entrée pour le conteneur Docker
# Démarre sysrepo-plugind puis exécute les tests

echo "🚀 Démarrage de l'environnement de test RESTCONF"
echo ""

# Démarrer sysrepo-plugind en arrière-plan
echo "🔌 Démarrage de sysrepo-plugind..."
sysrepo-plugind &
SYSREPO_PID=$!

# Attendre que sysrepo soit prêt
echo "⏳ Attente que sysrepo soit prêt..."
for i in $(seq 1 30); do
    if sysrepoctl -l > /dev/null 2>&1; then
        echo "✅ sysrepo-plugind est prêt"
        break
    fi
    sleep 0.5
done

echo ""
echo "📋 Liste des modules YANG chargés:"
sysrepoctl -l || true

echo ""
echo "📋 Liste des plugins Datastore / Notification chargés:"
sysrepoctl -L || true

echo ""
echo "🏃 Exécution des tests..."
echo ""

# Exécuter pytest avec les tests
cd /workspace
exec pytest test/ -vv "$@"

# Arrêt propre (ne sera atteint que si pytest échoue avant de démarrer)
echo ""
echo "🛑 Arrêt de sysrepo-plugind..."
kill $SYSREPO_PID 2>/dev/null || true
wait $SYSREPO_PID 2>/dev/null || true
