#!/bin/sh -ex

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

# Fonction pour installer un module YANG
install_yang_module() {
    local module_name=$1
    local module_file=$2
    if sysrepoctl -l | grep -q "${module_name}"; then
        echo "✅ Module ${module_name} est déjà chargé"
    else
        echo "⚠️  Module ${module_name} non chargé, tentative d'installation..."
        if [ -f "${module_file}" ]; then
            sysrepoctl -i "${module_file}" || echo "❌ Échec de l'installation de ${module_name}"
        else
            echo "❌ Fichier ${module_file} non trouvé"
        fi
    fi
}

# Fonction pour charger un plugin
load_plugin() {
    local plugin_name=$1
    local plugin_file=$2
    if sysrepoctl -p | grep -q "${plugin_name}"; then
        echo "✅ Plugin ${plugin_name} est déjà chargé"
    else
        echo "⚠️  Plugin ${plugin_name} non chargé, tentative de chargement..."
        if [ -f "${plugin_file}" ]; then
            sysrepoctl -P "${plugin_file}" || echo "❌ Échec du chargement de ${plugin_name}"
        else
            echo "❌ Fichier ${plugin_file} non trouvé"
        fi
    fi
}

# Installer les modules YANG
install_yang_module "restconf-test" "/usr/local/share/sysrepo/yang/restconf-test.yang"
install_yang_module "oven" "/usr/local/share/sysrepo/yang/oven.yang"

# Charger les plugins
load_plugin "sr_plugin_oven" "/usr/local/lib/sysrepo/plugins/sr_plugin_oven.so"
load_plugin "sr_plugin_restconf-test" "/usr/local/lib/sysrepo/plugins/sr_plugin_restconf-test.so"

# Vérifier également dans d'autres chemins courants
install_yang_module "oven" "/usr/share/sysrepo/yang/oven.yang"
load_plugin "sr_plugin_oven" "/usr/lib/sysrepo/plugins/sr_plugin_oven.so"
load_plugin "sr_plugin_oven" "/usr/lib/sysrepo/plugins/examples/oven.so"

echo ""
echo "📋 Liste des modules YANG chargés:"
sysrepoctl -l || true

echo ""
echo "📋 Liste des plugins chargés:"
sysrepoctl -p || true

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
