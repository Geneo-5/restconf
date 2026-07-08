# RESTCONF Qualification Test Suite

**Documentation complète des tests de conformité RESTCONF pour les RFC 8040, 8527, 7950, 7951, 6243 et 6415.**

---

## 📋 À PROPOS

Ce dossier contient **toute la documentation et les artefacts nécessaires** pour qualifier un serveur RESTCONF selon les standards IETF.

- **350+ cas de test** couvrant toutes les fonctionnalités RESTCONF
- **Deux modules YANG** : `oven.yang` (exemple simple) et `restconf-test.yang` (complet)
- **Stratégie de test** structurée par phases
- **Exemples concrets** avec code Python

---

## 🎯 ORGANISATION

```
doc/test/
├── README.md                          # Ce fichier
├── STRATEGY.md                        # Stratégie de test complète
├── modules/                          # Modules YANG pour les tests
│   ├── oven.yang                      # Module Sysrepo (exemple simple)
│   └── restconf-test.yang             # Module complet pour qualification
├── cases/                            # Cas de test par domaine
│   ├── infrastructure/               # Phase 2 : Infrastructure RESTCONF
│   │   ├── README.md
│   │   └── TC-2-XXX.md (cas de test)
│   ├── crud/                          # Phase 3 : Opérations CRUD
│   │   ├── README.md
│   │   ├── TC-3-001_get_data_root.md
│   │   ├── TC-3-002_get_container.md
│   │   └── ...
│   ├── parameters/                   # Phase 4 : Paramètres de requête
│   ├── rpc/                           # Phase 5 : RPC, Actions, Notifications
│   ├── nmda/                          # Phase 6 : Datastores NMDA
│   ├── errors/                        # Phase 7 : Gestion des erreurs
│   ├── security/                      # Phase 8 : Sécurité
│   └── performance/                   # Phase 9 : Performance
├── scenarios/                        # Scénarios end-to-end
│   ├── SQ-001_creation_interface.md
│   ├── SQ-002_modification_interface.md
│   └── ...
├── matrices/                         # Matrices de conformité
│   ├── rfc8040-matrix.md
│   ├── rfc8527-matrix.md
│   └── complete-matrix.md
├── datasets/                         # Jeux de données de test
│   ├── DATA-001_minimal_interface.json
│   ├── DATA-002_complete_interface.json
│   └── ...
└── examples/                         # Exemples de code
    └── oven/                          # Exemples avec oven.yang
        ├── README.md
        ├── test_oven_basic.py        # Test case simple
        └── test_oven_complete.py
```

---

## 🚀 DÉMARRAGE RAPIDE

### 1. Prérequis

- Serveur RESTCONF fonctionnel (Sysrepo, Netopeer2, etc.)
- Python 3.8+ avec `requests` et `pytest`
- Le module **oven.yang** ou **restconf-test.yang** chargé sur le serveur

### 2. Vérification du module

```bash
# Vérifier qu'oven.yang est chargé
curl -u user:password http://localhost:8080/restconf/data/oven:oven

# Vérifier que restconf-test.yang est chargé
curl -u user:password http://localhost:8080/restconf/data/rt:restconf-test
```

### 3. Exécuter un test simple

```bash
# Se placer dans le répertoire des tests
cd doc/test/examples/oven

# Exécuter le test simple avec oven.yang
python test_oven_basic.py

# Exécuter avec pytest (si installé)
pytest test_oven_basic.py -v
```

---

## 📊 STRUCTURE DES TESTS

### Modules YANG

| Module | Namespace | Description | Utilisation |
|--------|-----------|-------------|-------------|
| **oven.yang** | `urn:sysrepo:oven` | Module Sysrepo simple (four) | **Exemples** et tests basiques |
| **restconf-test.yang** | `urn:restconf:test` | Module complet pour qualification | **Tous les tests RFC** |

### Phases de Test

| Phase | Domaine | Cas de Test | Module Recommandé |
|-------|---------|-------------|-------------------|
| 1 | Infrastructure | TC-2-001 à TC-2-040 | Les deux |
| 2 | CRUD | TC-3-001 à TC-3-050 | **restconf-test.yang** |
| 3 | Paramètres | TC-4-001 à TC-4-055 | **restconf-test.yang** |
| 4 | RPC/Notifications | TC-5-001 à TC-5-040 | **restconf-test.yang** |
| 5 | NMDA | TC-6-001 à TC-6-039 | **restconf-test.yang** |
| 6 | Erreurs | TC-7-001 à TC-7-067 | **restconf-test.yang** |
| 7 | Sécurité | TC-8-001 à TC-8-020 | **restconf-test.yang** |
| 8 | Performance | TC-9-001 à TC-9-020 | **restconf-test.yang** |

> **⚠️ IMPORTANT** : Pour les tests de qualification complète (Phases 2-8), **le module `restconf-test.yang` DOIT être chargé** sur le serveur. Le module `oven.yang` est insuffisant pour la plupart des cas de test.

---

## 🎓 EXEMPLES PÉDAGOGIQUES

### Avec oven.yang (Sysrepo)

Le module `oven.yang` est **l'exemple parfait pour démarrer** :
- Simple et compréhensible
- Contient les concepts de base : containers, leaves, RPC, notifications
- Idéal pour apprendre RESTCONF

**Fichiers** :
- [`modules/oven.yang`](modules/oven.yang) - Le module YANG
- [`examples/oven/test_oven_basic.py`](examples/oven/test_oven_basic.py) - Test case simple
- [`examples/oven/README.md`](examples/oven/README.md) - Explications détaillées

### Avec restconf-test.yang (Qualification)

Le module `restconf-test.yang` est **conçu pour la qualification complète** :
- Couvre **toutes les fonctionnalités RFC** 
- Contient des structures complexes : listes, leaf-lists, contraintes, types avancés
- Nécessaire pour valider la conformité aux RFCs

**Fichiers** :
- [`modules/restconf-test.yang`](modules/restconf-test.yang) - Le module complet
- [`cases/`](cases/) - Tous les cas de test (350+)
- [`matrices/`](matrices/) - Matrices de conformité RFC

---

## 📚 DOCUMENTATION

| Document | Description |
|----------|-------------|
| [STRATEGY.md](STRATEGY.md) | Stratégie complète de test |
| [cases/](cases/) | Cas de test détaillés par domaine |
| [scenarios/](scenarios/) | Scénarios end-to-end |
| [matrices/](matrices/) | Matrices de conformité RFC |
| [datasets/](datasets/) | Jeux de données JSON |

---

## 🔧 INTÉGRATION CI/CD

### Avec pytest

```yaml
# .github/workflows/test.yml
name: RESTCONF Qualification

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Set up Python
        uses: actions/setup-python@v4
        with:
          python-version: '3.10'
          
      - name: Install dependencies
        run: |
          python -m pip install --upgrade pip
          pip install requests pytest
          
      - name: Run tests
        run: |
          cd doc/test
          # Vérifier que le module est chargé
          python examples/oven/test_oven_basic.py
          # Exécuter tous les tests
          # pytest cases/ -v
```

### Avec CTest

Voir la [ROADMAP](../../ROADMAP.md) pour l'intégration avec CTest.

---

## 📞 SUPPORT

- **Questions sur la stratégie** : Voir [STRATEGY.md](STRATEGY.md)
- **Questions sur un cas de test spécifique** : Voir [cases/](cases/)
- **Problèmes avec les modules YANG** : Voir [modules/](modules/)
- **Exemples de code** : Voir [examples/](examples/)

---

## 🎯 PROCHAINES ÉTAPES

1. **Lire la stratégie** : [STRATEGY.md](STRATEGY.md)
2. **Essayer l'exemple simple** : [examples/oven/test_oven_basic.py](examples/oven/test_oven_basic.py)
3. **Explorer les cas de test** : [cases/](cases/)
4. **Vérifier la matrice de conformité** : [matrices/complete-matrix.md](matrices/complete-matrix.md)
5. **Charger restconf-test.yang** sur ton serveur pour les tests complets

---

## 📜 HISTORIQUE

- **2026-07-08** : Réorganisation du dossier `.draft/` → `doc/test/`
- **2026-07-08** : Création de `restconf-test.yang` pour la couverture complète
- **2026-07-08** : Ajout des matrices de conformité et cas de test détaillés

---

## 📄 LICENSE

Ce travail est sous licence MIT. Voir le fichier [LICENSE](../../LICENSE) pour plus de détails.
