# Jeux de Données

## ⚠️ Module YANG de référence

**Module à utiliser** : `restconf-test.yang` (namespace: `urn:restconf:test`, prefix: `rt`)

Tous les jeux de données ci-dessous utilisent le module `restconf-test.yang`.

---

## Jeux de données pour restconf-test.yang

### DATA-001 — Interface minimale

**Module** : `rt:restconf-test`
**URI** : `/restconf/data/rt:restconf-test/rt:interfaces/rt:interface=eth0`

```json
{
  "rt:interface": [
    {
      "name": "eth0",
      "description": "Interface Ethernet minimale",
      "enabled": true,
      "mtu": 1500
    }
  ]
}
```

### DATA-002 — Interface complète

**Module** : `rt:restconf-test`
**URI** : `/restconf/data/rt:restconf-test/rt:interfaces/rt:interface=eth0`

```json
{
  "rt:interface": [
    {
      "name": "eth0",
      "description": "Interface Ethernet complète avec toutes les propriétés",
      "enabled": true,
      "mtu": 9000
    }
  ]
}
```

### DATA-003 — Configuration invalide

**Module** : `rt:restconf-test`
**Objectif** : Tester la détection des erreurs

```json
{
  "rt:basic-data": {
    "timeout": "invalide"  // Doit être uint16, pas string
  }
}
```

### DATA-004 — Liste de 1000 interfaces

**Module** : `rt:restconf-test`
**URI** : `/restconf/data/rt:restconf-test/rt:interfaces`

Générer 1000 entrées avec :
```json
{
  "rt:interface": [
    {"name": "eth0", "enabled": true, "mtu": 1500},
    {"name": "eth1", "enabled": true, "mtu": 1500},
    // ... 998 autres
    {"name": "eth999", "enabled": true, "mtu": 1500}
  ]
}
```

### DATA-005 — Caractères UTF-8

**Module** : `rt:restconf-test`

```json
{
  "rt:basic-data": {
    "device-id": "système-émulateur",
    "description": "Configuration avec caractères accentués : éàèçû"
  }
}
```

### DATA-006 — Violation de contrainte MUST

**Module** : `rt:restconf-test`
**URI** : `/restconf/data/rt:restconf-test/rt:constraints/rt:must-test`

```json
{
  "rt:must-test": {
    "start-time": 100,
    "end-time": 50  // Violation : start-time >= end-time
  }
}
```

### DATA-007 — Violation de LEAFREF

**Module** : `rt:restconf-test`
**URI** : `/restconf/data/rt:restconf-test/rt:advanced-types`

```json
{
  "rt:advanced-types": {
    "default-interface": "nonexistent"  // Référence vers interface inexistante
  }
}
```

### DATA-008 — ENUM invalide

**Module** : `rt:restconf-test`
**URI** : `/restconf/data/rt:restconf-test/rt:constraints`

```json
{
  "rt:constraints": {
    "operation-mode": "invalid-mode"  // Pas dans l'énumération
  }
}
```

### DATA-009 — Configuration maximale

**Module** : `rt:restconf-test`
**Objectif** : Tester les limites du serveur

Créer une configuration avec :
- Toutes les listes remplies à max-elements
- Tous les leaf-lists à max-elements
- Toutes les contraintes à leurs limites

### DATA-010 — Datastore complet

**Module** : `rt:restconf-test`
**Objectif** : Peupler tous les datastores NMDA

```bash
# Running datastore
POST /restconf/ds/ietf-datastores:running/rt:restconf-test
# Operational datastore (lecture seulement)
GET /restconf/ds/ietf-datastores:operational/rt:restconf-test
# Intended datastore
GET /restconf/ds/ietf-datastores:intended/rt:restconf-test
```

---

## Génération des données

Les jeux de données peuvent être générés dynamiquement avec Python :

```python
import json

def generate_data_set(set_name, count=1000):
    data = {
        "rt:interface": []
    }
    for i in range(count):
        data["rt:interface"].append({
            "name": f"eth{i}",
            "description": f"Interface générée {i}",
            "enabled": True,
            "mtu": 1500
        })
    return json.dumps(data, indent=2)
```
