# Oven Module - RESTCONF Example

**Exemples pédagogiques et cas de test simples pour le module `oven.yang`**

Le module `oven.yang` est **l'exemple parfait pour démarrer** avec RESTCONF. Il s'agit d'un module YANG simple mais complet, initialement fourni avec [Sysrepo](https://github.com/sysrepo/sysrepo), qui couvre les concepts fondamentaux :

- Containers de configuration et d'état
- Leafs avec types de données
- RPC (Remote Procedure Calls)
- Notifications
- Typedefs

## 📋 Structure

```
oven/
├── README.md          # Ce fichier
├── test_oven_basic.py # Cas de test simples (10 tests)
└── oven.yang          # Le module YANG (dans ../modules/)
```

## 🎯 Module oven.yang

### Description

Le module `oven` (four en anglais) représente un four domestique simple avec les fonctionnalités suivantes :

- **Configuration** (`/oven:oven`) :
  - `turned-on` : Interrupteur principal (boolean)
  - `temperature` : Température configurée (0-250°C)

- **État** (`/oven:oven-state`) :
  - `temperature` : Température actuelle dans le four
  - `food-inside` : Indique si de la nourriture est dans le four

- **RPC** :
  - `insert-food` : Met la nourriture dans le four
  - `remove-food` : Sort la nourriture du four

- **Notifications** :
  - `oven-ready` : Notifie quand la température configurée est atteinte

### Namespace

- **URI** : `urn:sysrepo:oven`
- **Prefix** : `ov`
- **Révision** : 2018-01-19

### Schéma YANG

Voir le fichier complet : [`../../modules/oven.yang`](../../modules/oven.yang)

## 🚀 Configuration du Serveur

### Avec Sysrepo + sysrepocfg

```bash
# Installer Sysrepo et sysrepocfg (sur Ubuntu/Debian)
sudo apt update
sudo apt install sysrepo sysrepocfg

# Démarrer le démon Sysrepo
sudo systemctl start sysrepo
sudo systemctl enable sysrepo

# Charger le module oven.yang
sysrepocfg --import /chemin/vers/oven.yang

# Vérifier que le module est chargé
sysrepoctl -l
# Devrait afficher : oven

# Démarrer le serveur RESTCONF (Netopeer2)
netopeer2-server -d
```

### Avec Netopeer2 (directement)

```bash
# Installer Netopeer2
sudo apt install netopeer2-server netopeer2-cli

# Copier oven.yang dans le répertoire des modules
sudo cp oven.yang /etc/netopeer2/modules/

# Redémarrer le serveur
sudo systemctl restart netopeer2-server
```

### Vérification du Module

```bash
# Vérifier que le module est accessible
curl -u admin:admin http://localhost:8080/restconf/data/oven:oven

# Devrait retourner : 200 OK avec la configuration (vide si pas encore configuré)
# {
#   "oven:oven": {
#     "turned-on": false,
#     "temperature": 0
#   }
# }
```

## 🎓 Exécution des Tests

### Prérequis

- Python 3.8+
- Bibliothèques Python : `requests`

```bash
pip install requests
```

### Exécuter tous les tests

```bash
# Se placer dans le répertoire
cd /chemin/vers/restconf/doc/test/examples/oven

# Exécuter le script de test
python test_oven_basic.py

# Ou avec les paramètres personnalisés
python test_oven_basic.py --url http://192.168.1.100:8080/restconf --user admin --password secret
```

### Exécuter un test spécifique

```bash
# Lister tous les tests disponibles
python test_oven_basic.py --list

# Exécuter un test spécifique
python test_oven_basic.py --test test_003_get_oven_config
```

### Exemples de Sortie

```
============================================================
  RESTCONF Basic Test Suite - oven.yang
============================================================

[TC-001] RESTCONF Discovery
  Checking /.well-known/host-meta...
  ✓ PASS: Server supports RESTCONF discovery

[TC-002] Root Resource Access
  Checking /restconf...
  ✓ PASS: Root resource accessible

[TC-003] GET Oven Configuration
  Reading /restconf/data/oven:oven...
  ✓ PASS: Retrieved oven config
  Data: {
    "oven:oven": {
        "turned-on": false,
        "temperature": 0
    }
}

...

============================================================
  Results: 10/10 tests passed
============================================================
```

## 📚 Cas de Test

Les 10 cas de test de `test_oven_basic.py` couvrent les opérations RESTCONF fondamentales :

### Infrastructure (2 tests)
- **TC-001** : Découverte RESTCONF (`/.well-known/host-meta`)
- **TC-002** : Accès à la ressource racine

### CRUD - Create, Read, Update, Delete (4 tests)
- **TC-003** : GET - Lecture de la configuration du four
- **TC-004** : GET - Lecture de l'état du four
- **TC-005** : PUT - Création/remplacement de la configuration
- **TC-006** : PATCH - Modification partielle (température seulement)
- **TC-007** : DELETE - Suppression de la configuration
- **TC-010** : GET avec filtre - Lecture d'une leaf spécifique

### RPC (2 tests)
- **TC-008** : RPC insert-food - Mettre la nourriture dans le four
- **TC-009** : RPC remove-food - Sortir la nourriture du four

## 🎯 Bonnes Pratiques

### 1. Toujours vérifier la découverte RESTCONF

Avant toute opération, vérifiez que le serveur supporte RESTCONF :

```python
response = requests.get("http://server/.well-known/host-meta")
assert response.status_code == 200
```

### 2. Utiliser les bons Content-Type

```python
headers = {
    'Accept': 'application/yang-data+json',
    'Content-Type': 'application/yang-data+json',
}
```

### 3. Gérer les erreurs

```python
import requests
from requests.exceptions import RequestException

try:
    response = requests.get(url, auth=auth, headers=headers)
    response.raise_for_status()  # Lève une exception pour les codes 4xx/5xx
except RequestException as e:
    print(f"Request failed: {e}")
    # Gérer l'erreur
```

### 4. Utiliser les namespaces correctement

Dans RESTCONF, les namespaces doivent être préfixés :

```python
# Bon
url = "/restconf/data/oven:oven"

# Mauvais (peut causer des erreurs)
url = "/restconf/data/oven"
```

### 5. Vérifier les codes de retour HTTP

| Opération | Codes attendus |
|-----------|----------------|
| GET | 200 OK |
| PUT (création) | 201 Created |
| PUT (modification) | 204 No Content |
| PATCH | 204 No Content |
| DELETE | 204 No Content |
| POST (RPC) | 200 OK |

## 📝 Notes

### Limitations

Le module `oven.yang` est **simple** et ne couvre pas toutes les fonctionnalités RESTCONF :

- ❌ Pas de listes (`list`)
- ❌ Pas de leaf-lists (`leaf-list`)
- ❌ Pas de choix (`choice`)
- ❌ Pas de cas (`case`)
- ❌ Pas d'augmentations (`augment`)
- ❌ Pas de devoirs (`must`)
- ❌ Pas de when (`when`)

Pour tester ces fonctionnalités avancées, utilisez le module [`restconf-test.yang`](../../modules/restconf-test.yang).

### Comparaison avec restconf-test.yang

| Fonctionnalité | oven.yang | restconf-test.yang |
|----------------|-----------|---------------------|
| Complexité | ⭐ | ⭐⭐⭐⭐⭐ |
| Containers | ✅ | ✅ |
| Lists | ❌ | ✅ |
| Leaf-lists | ❌ | ✅ |
| Typedefs | ✅ (1) | ✅ (20+) |
| Identities | ❌ | ✅ |
| RPC | ✅ (2) | ✅ (10+) |
| Notifications | ✅ (1) | ✅ (5+) |
| NMDA support | ❌ | ✅ |
| Contraintes | ❌ | ✅ |
| Couverture RFC | Partielle | **Complète** |

**→ Utilisez `oven.yang` pour apprendre, `restconf-test.yang` pour qualifier.**

## 🔗 Liens Utiles

- [Module oven.yang original (Sysrepo)](https://github.com/sysrepo/sysrepo/blob/master/examples/ietf-yang-library/src/yang/ietf-yang-library.yang)
- [RFC 8040 - RESTCONF Protocol](https://datatracker.ietf.org/doc/html/rfc8040)
- [Sysrepo Documentation](https://github.com/sysrepo/sysrepo)
- [Netopeer2 Documentation](https://github.com/CESNET/netopeer2)

## 📞 Support

Pour toute question ou problème :
1. Vérifiez que le module est correctement chargé sur le serveur
2. Consultez les logs du serveur RESTCONF
3. Vérifiez les permissions d'accès
4. Consultez la [stratégie de test](../../STRATEGY.md) pour plus de détails

---

*Dernière mise à jour : 2026-07-09*
