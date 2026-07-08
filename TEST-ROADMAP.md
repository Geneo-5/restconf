# 🗺️ TEST-ROADMAP - Feuille de Route des Tests RESTCONF

**Document de référence pour l'implémentation complète des tests de conformité RESTCONF**

Ce document liste **toutes les tâches** à implémenter pour valider la conformité du serveur RESTCONF aux RFCs IETF.

> **⚠️ Prérequis** : Tous les tests de qualification **DOIVENT** vérifier que le module `restconf-test.yang` (namespace: `urn:restconf:test`, prefix: `rt`) est chargé et accessible via RESTCONF avant leur exécution. Le module `oven.yang` est utilisé pour les exemples simples.

---

## 📊 **Tableau de Bord Global**

| Phase | Description | RFC | Tests Implémentés | Tests Restants | Statut |
|-------|-------------|-----|-------------------|----------------|--------|
| **1** | Infrastructure RESTCONF | RFC 8040 §2-3 | 9/9 | 0 | 🟢 **Terminé** |
| **2** | Sécurité & JWT | RFC 8040 §4, RFC 7515-7519 | 0/6 | 6 | ⚪ À faire |
| **3** | Opérations CRUD | RFC 8040 §4.2-4.7 | 0/20 | 20 | ⚪ À faire |
| **4** | Paramètres de Requête | RFC 8040 §4.8 | 0/15 | 15 | ⚪ À faire |
| **5** | RPC, Actions, Notifications | RFC 8040 §5-6, RFC 7950 §7.15-7.16 | 0/25 | 25 | ⚪ À faire |
| **6** | NMDA (Datastores) | RFC 8527 | 0/10 | 10 | ⚪ À faire |
| **7** | Gestion des Erreurs | RFC 8040 §7 | 3/15 | 12 | 🟡 Partiel |
| **8** | Sécurité Avancée | RFC 8341 | 0/10 | 10 | ⚪ À faire |
| **9** | Performance & Robustesse | RFC 8040 §8 | 0/10 | 10 | ⚪ À faire |
| **10** | Intégration CI/CD | - | 0/5 | 5 | ⚪ À faire |

**Total : 9/115 tests implémentés (7.8%)** | **106 tests restants**

---

## 🎯 **Organisation par Phase et Priorité**

### Phase 1 : Infrastructure RESTCONF (RFC 8040 §2-3)
*Objectif : Vérifier que le serveur expose correctement son interface RESTCONF.*

#### ✅ **Déjà implémenté dans `test/test_basic.py`**

| ID | Test | RFC | Classe | Méthode | Statut |
|----|------|-----|--------|---------|--------|
| TC-2-001 | Host Metadata (XRD XML) | RFC 8040 §3.1 | `TestRootDiscovery` | `test_host_meta` | ✅ **Fait** |
| TC-2-002 | Host Metadata Content | RFC 8040 §3.1 | `TestRootDiscovery` | `test_host_meta_content` | ✅ **Fait** |
| TC-2-003 | Host Metadata JSON | RFC 8040 §3.1 | `TestRootDiscovery` | `test_host_meta_json` | ✅ **Fait** |
| TC-2-004 | API Resource (JSON) | RFC 8040 §3.2 | `TestAPIResource` | `test_api_resource_json` | ✅ **Fait** |
| TC-2-005 | API Resource (XML) | RFC 8040 §3.2 | `TestAPIResource` | `test_api_resource_xml` | ✅ **Fait** |
| TC-2-006 | API Resource Content | RFC 8040 §3.2 | `TestAPIResource` | `test_api_resource_content` | ✅ **Fait** |
| TC-2-007 | Unsupported Media Type | RFC 8040 §3.5.2 | `TestAPIResource` | `test_unsupported_media_type` | ✅ **Fait** |
| TC-2-008 | HEAD Method | RFC 8040 §3.4 | `TestHTTPMethods` | `test_head` | ✅ **Fait** |
| TC-2-009 | OPTIONS Method | RFC 8040 §3.4 | `TestHTTPMethods` | `test_options` | ✅ **Fait** |

#### 📝 **Documentation associée**
- `doc/test/cases/infrastructure/README.md` - Validation de l'infrastructure RESTCONF
- `doc/test/cases/infrastructure/RESOURCES.md` - Tests des ressources RESTCONF

---

### Phase 2 : Sécurité & Authentification JWT (RFC 8040 §4, RFC 7515-7519)
*Objectif : Tester l'authentification et l'autorisation.*

| ID | Test | RFC | Description | Statut |
|----|------|-----|-------------|--------|
| TC-2-010 | Authentification requise | RFC 8040 §4 | GET sans auth → 401 | ⚪ À faire |
| TC-2-011 | Authentification réussie | RFC 8040 §4 | JWT valide → 200 | ⚪ À faire |
| TC-2-012 | JWT invalide | RFC 7515 | Signature invalide → 401 | ⚪ À faire |
| TC-2-013 | JWT expiré | RFC 7519 | Token expiré → 401 | ⚪ À faire |
| TC-2-014 | JWT manquant | RFC 8040 §4 | Pas d'Authorization header → 401 | ⚪ À faire |
| TC-2-015 | NACM - Accès autorisé | RFC 8341 | User avec permissions → 200 | ⚪ À faire |

**Fichier cible** : `test/test_security.py` (à créer)

---

### Phase 3 : Opérations CRUD (RFC 8040 §4.2-4.7)
*Objectif : Tester Create, Read, Update, Delete sur les données.*

**Prérequis** : Le module `restconf-test.yang` **DOIT** être chargé.

| ID | Test | RFC | Description | Statut |
|----|------|-----|-------------|--------|
| TC-3-001 | GET - Racine data | RFC 8040 §4.2 | GET /restconf/data | ⚪ À faire |
| TC-3-002 | GET - Container | RFC 8040 §4.2 | GET /restconf/data/rt:system | ⚪ À faire |
| TC-3-003 | GET - Leaf | RFC 8040 §4.2 | GET /restconf/data/rt:system/rt:config/rt:system-name | ⚪ À faire |
| TC-3-004 | GET - List | RFC 8040 §4.2 | GET /restconf/data/rt:interfaces/rt:interface | ⚪ À faire |
| TC-3-005 | GET - List avec key | RFC 8040 §4.2 | GET /restconf/data/rt:interfaces/rt:interface[name='eth0'] | ⚪ À faire |
| TC-3-006 | POST - Créer dans list | RFC 8040 §4.4 | POST /restconf/data/rt:interfaces/rt:interface | ⚪ À faire |
| TC-3-007 | PUT - Créer/remplacer | RFC 8040 §4.5 | PUT /restconf/data/rt:system/rt:config | ⚪ À faire |
| TC-3-008 | PUT - Modifier | RFC 8040 §4.5 | PUT /restconf/data/rt:interfaces/rt:interface[name='eth0'] | ⚪ À faire |
| TC-3-009 | PATCH - Modifier partiel | RFC 8040 §4.6 | PATCH /restconf/data/rt:system/rt:config | ⚪ À faire |
| TC-3-010 | DELETE - Supprimer | RFC 8040 §4.7 | DELETE /restconf/data/rt:interfaces/rt:interface[name='eth0'] | ⚪ À faire |
| TC-3-011 | GET - Données inexistantes | RFC 8040 §4.2 | GET /restconf/data/rt:nonexistent → 404 | ⚪ À faire |
| TC-3-012 | POST - URI existante | RFC 8040 §4.4 | POST sur ressource existante → 409 | ⚪ À faire |
| TC-3-013 | HEAD - Data resource | RFC 8040 §4.3 | HEAD /restconf/data/rt:system | ⚪ À faire |
| TC-3-014 | OPTIONS - Data resource | RFC 8040 §4.3 | OPTIONS /restconf/data → Allow header | ⚪ À faire |
| TC-3-015 | ETag support | RFC 8040 §3.4.1 | Vérifier ETag headers | ⚪ À faire |
| TC-3-016 | Last-Modified support | RFC 8040 §3.4.1 | Vérifier Last-Modified headers | ⚪ À faire |
| TC-3-017 | Read-only leaf | RFC 7950 §7.6 | Tentative PUT sur leaf config=false → 400 | ⚪ À faire |
| TC-3-018 | Mandatory leaf | RFC 7950 §7.6 | PUT sans mandatory leaf → 400 | ⚪ À faire |
| TC-3-019 | Leaf avec default | RFC 6243 | Vérifier que default est appliqué | ⚪ À faire |
| TC-3-020 | Multiple datastores | RFC 8040 §1.4 | GET /restconf/data vs /restconf/ds | ⚪ À faire |

**Fichier cible** : `test/test_crud.py` (à créer)

---

### Phase 4 : Paramètres de Requête (RFC 8040 §4.8)
*Objectif : Tester les query parameters RESTCONF.*

**Prérequis** : Le module `restconf-test.yang` **DOIT** être chargé.

| ID | Test | RFC | Description | Statut |
|----|------|-----|-------------|--------|
| TC-4-001 | content=config | RFC 8040 §4.8.1 | Filtrer pour config seulement | ⚪ À faire |
| TC-4-002 | content=nonconfig | RFC 8040 §4.8.1 | Filtrer pour nonconfig seulement | ⚪ À faire |
| TC-4-003 | content=all | RFC 8040 §4.8.1 | Retourner tout | ⚪ À faire |
| TC-4-004 | depth=1 | RFC 8040 §4.8.2 | Limiter la profondeur | ⚪ À faire |
| TC-4-005 | depth=2 | RFC 8040 §4.8.2 | Profondeur 2 | ⚪ À faire |
| TC-4-006 | depth=unbounded | RFC 8040 §4.8.2 | Pas de limite | ⚪ À faire |
| TC-4-007 | fields - simple | RFC 8040 §4.8.3 | Sélection de champs | ⚪ À faire |
| TC-4-008 | fields - nested | RFC 8040 §4.8.3 | Champs imbriqués | ⚪ À faire |
| TC-4-009 | fields - multiple | RFC 8040 §4.8.3 | Plusieurs champs | ⚪ À faire |
| TC-4-010 | with-defaults=report-all | RFC 8040 §4.8.4 | Tous les defaults | ⚪ À faire |
| TC-4-011 | with-defaults=trim | RFC 8040 §4.8.4 | Supprimer defaults | ⚪ À faire |
| TC-4-012 | with-defaults=explicit | RFC 8040 §4.8.4 | Defaults explicites | ⚪ À faire |
| TC-4-013 | with-origin | RFC 8040 §4.8.5 | Origine des données | ⚪ À faire |
| TC-4-014 | Query invalide | RFC 8040 §4.8 | Parameter inconnu → 400 | ⚪ À faire |
| TC-4-015 | Combinaison de queries | RFC 8040 §4.8 | Plusieurs params ensemble | ⚪ À faire |

**Fichier cible** : `test/test_query_params.py` (à créer)

---

### Phase 5 : RPC, Actions, Notifications (RFC 8040 §5-6, RFC 7950 §7.15-7.16)
*Objectif : Tester les opérations RPC, les actions YANG 1.1 et les notifications.*

**Prérequis** : Le module `restconf-test.yang` **DOIT** être chargé.

#### RPC Tests

| ID | Test | RFC | Description | Statut |
|----|------|-----|-------------|--------|
| TC-5-001 | RPC Discovery | RFC 8040 §3.2 | GET /restconf/operations | ⚪ À faire |
| TC-5-002 | RPC sans paramètres | RFC 8040 §5 | POST /restconf/operations/rt:get-system-status | ⚪ À faire |
| TC-5-003 | RPC avec paramètres | RFC 8040 §5 | POST /restconf/operations/rt:configure-device | ⚪ À faire |
| TC-5-004 | RPC avec param mandatory | RFC 8040 §5 | POST sans param mandatory → 400 | ⚪ À faire |
| TC-5-005 | RPC avec type validation | RFC 8040 §5 | Param invalide → 400 | ⚪ À faire |
| TC-5-006 | RPC inexistant | RFC 8040 §5 | POST /restconf/operations/rt:nonexistent → 404 | ⚪ À faire |
| TC-5-007 | RPC output | RFC 8040 §5 | Vérifier output RPC | ⚪ À faire |
| TC-5-008 | RPC sans output | RFC 8040 §5 | RPC sans return → 204 | ⚪ À faire |

#### Actions Tests (YANG 1.1)

| ID | Test | RFC | Description | Statut |
|----|------|-----|-------------|--------|
| TC-5-015 | Action sans paramètres | RFC 7950 §7.15 | POST /restconf/data/rt:device-management/rt:reset | ⚪ À faire |
| TC-5-016 | Action avec paramètres | RFC 7950 §7.15 | POST /restconf/data/rt:device-management/rt:test-connection | ⚪ À faire |
| TC-5-017 | Action sur ressource | RFC 7950 §7.15 | POST /restconf/data/rt:device-management/rt:managed-device=1/rt:reboot | ⚪ À faire |

#### Notifications Tests

| ID | Test | RFC | Description | Statut |
|----|------|-----|-------------|--------|
| TC-5-020 | Stream subscription | RFC 8040 §6 | GET /restconf/stream/netconf:notify | ⚪ À faire |
| TC-5-021 | Notification reception | RFC 8040 §6 | Recevoir notification | ⚪ À faire |
| TC-5-022 | Filter notifications | RFC 5277 | Filtre SSE | ⚪ À faire |
| TC-5-023 | Notification encoding | RFC 8040 §6 | JSON vs XML | ⚪ À faire |

**Fichier cible** : `test/test_rpc.py`, `test/test_notifications.py` (à créer)

---

### Phase 6 : NMDA - Network Management Datastore Architecture (RFC 8527)
*Objectif : Tester le support des multiples datastores.*

**Prérequis** : Le serveur **DOIT** supporter RFC 8527.

| ID | Test | RFC | Description | Statut |
|----|------|-----|-------------|--------|
| TC-6-001 | List datastores | RFC 8527 §3 | GET /restconf/ds | ⚪ À faire |
| TC-6-002 | Datastore running | RFC 8527 §3 | GET /restconf/ds/running | ⚪ À faire |
| TC-6-003 | Datastore candidate | RFC 8527 §3 | GET /restconf/ds/candidate | ⚪ À faire |
| TC-6-004 | Datastore startup | RFC 8527 §3 | GET /restconf/ds/startup | ⚪ À faire |
| TC-6-005 | Datastore operational | RFC 8527 §3 | GET /restconf/ds/operational | ⚪ À faire |
| TC-6-006 | Edit running | RFC 8527 §4 | PUT /restconf/ds/running/... | ⚪ À faire |
| TC-6-007 | Edit candidate | RFC 8527 §4 | PUT /restconf/ds/candidate/... | ⚪ À faire |
| TC-6-008 | Commit candidate | RFC 8527 §5 | POST /restconf/commit | ⚪ À faire |
| TC-6-009 | Discard candidate | RFC 8527 §5 | POST /restconf/discard-changes | ⚪ À faire |
| TC-6-010 | Datastore non supporté | RFC 8527 | GET /restconf/ds/unsupported → 404 | ⚪ À faire |

**Fichier cible** : `test/test_nmda.py` (à créer)

---

### Phase 7 : Gestion des Erreurs (RFC 8040 §7)
*Objectif : Tester le format et le contenu des erreurs RESTCONF.*

| ID | Test | RFC | Description | Statut |
|----|------|-----|-------------|--------|
| TC-7-001 | 400 Bad Request | RFC 8040 §7.1 | Requête invalide | ⚪ À faire |
| TC-7-002 | 401 Unauthorized | RFC 8040 §7.1 | Authentification requise | ⚪ À faire |
| TC-7-003 | 403 Forbidden | RFC 8040 §7.1 | Accès refusé | ⚪ À faire |
| TC-7-004 | 404 Not Found | RFC 8040 §7.1 | Ressource inexistante | ✅ **Fait** (test_404_unknown_path) |
| TC-7-005 | 405 Method Not Allowed | RFC 8040 §7.1 | Méthode non supportée | ✅ **Fait** (test_method_not_allowed_on_api) |
| TC-7-006 | 406 Not Acceptable | RFC 8040 §7.1 | Media type non supporté | ✅ **Fait** (test_unsupported_media_type) |
| TC-7-007 | 409 Conflict | RFC 8040 §7.1 | Conflit de ressource | ⚪ À faire |
| TC-7-008 | 413 Payload Too Large | RFC 8040 §7.1 | Payload trop grand | ⚪ À faire |
| TC-7-009 | 415 Unsupported Media Type | RFC 8040 §7.1 | Content-Type invalide | ⚪ À faire |
| TC-7-010 | 500 Internal Server Error | RFC 8040 §7.1 | Erreur serveur | ⚪ À faire |
| TC-7-011 | Error response format | RFC 8040 §7.1 | Format ietf-restconf:errors | ⚪ À faire |
| TC-7-012 | Error tags | RFC 8040 §7.1 | error-tag, error-type | ⚪ À faire |
| TC-7-013 | Multiple errors | RFC 8040 §7.1 | Plusieurs erreurs | ⚪ À faire |
| TC-7-014 | Invalid URI encoding | RFC 8040 §3.5.3 | URI mal encodée | ✅ **Fait** (test_bad_uri_format) |

**Fichiers existants** : `test/test_basic.py::TestErrorHandling` (3 tests)

---

### Phase 8 : Sécurité Avancée (RFC 8341 - NACM)
*Objectif : Tester le contrôle d'accès.*

| ID | Test | RFC | Description | Statut |
|----|------|-----|-------------|--------|
| TC-8-001 | Accès autorisé | RFC 8341 | User avec rights → 200 | ⚪ À faire |
| TC-8-002 | Accès refusé | RFC 8341 | User sans rights → 403 | ⚪ À faire |
| TC-8-003 | Read-only user | RFC 8341 | User read-only, PUT → 403 | ⚪ À faire |
| TC-8-004 | NACM groups | RFC 8341 | Tests par groupe | ⚪ À faire |
| TC-8-005 | NACM rules | RFC 8341 | Règles personnalisées | ⚪ À faire |
| TC-8-006 | HTTPS required | RFC 8040 §2 | h2c → OK, mais HTTPS recommandé | ⚪ À faire |
| TC-8-007 | Certificate validation | RFC 8040 §2.3 | Validation certif | ⚪ À faire |
| TC-8-008 | Client certificate | RFC 8040 §2.5 | Auth client | ⚪ À faire |
| TC-8-009 | Rate limiting | RFC 8040 | Protection DDoS | ⚪ À faire |
| TC-8-010 | CORS support | - | Cross-origin requests | ⚪ À faire |

**Fichier cible** : `test/test_security.py` (à créer)

---

### Phase 9 : Performance & Robustesse (RFC 8040 §8)
*Objectif : Tester la performance et la robustesse du serveur.*

| ID | Test | RFC | Description | Statut |
|----|------|-----|-------------|--------|
| TC-9-001 | Concurrent requests | RFC 8040 | 100 requêtes simultanées | ⚪ À faire |
| TC-9-002 | Large payload | RFC 8040 | Payload > 1MB | ⚪ À faire |
| TC-9-003 | Many resources | RFC 8040 | 1000 ressources | ⚪ À faire |
| TC-9-004 | Long-running connection | - | Connexion persistante | ⚪ À faire |
| TC-9-005 | Memory leak test | - | Test fuite mémoire | ⚪ À faire |
| TC-9-006 | Stress test | - | Charge maximale | ⚪ À faire |
| TC-9-007 | Slow client | - | Client lent | ⚪ À faire |
| TC-9-008 | Connection reset | - | Résilience | ⚪ À faire |
| TC-9-009 | Timeout handling | RFC 8040 | Gestion timeout | ⚪ À faire |
| TC-9-010 | Error recovery | - | Récupération après erreur | ⚪ À faire |

**Fichier cible** : `test/test_performance.py` (à créer)

---

### Phase 10 : Intégration CI/CD
*Objectif : Intégrer les tests dans le workflow CI/CD.*

| ID | Tâche | Description | Statut |
|----|-------|-------------|--------|
| TC-10-001 | CTest integration | `enable_testing()` + `add_test()` | ⚪ À faire |
| TC-10-002 | GitHub Actions | Workflow `.github/workflows/test.yml` | ⚪ À faire |
| TC-10-003 | Test coverage | > 80% couverture | ⚪ À faire |
| TC-10-004 | Smoke test script | Script de validation rapide | ⚪ À faire |
| TC-10-005 | Regression tests | Suite de tests de régression | ⚪ À faire |

---

## 📁 **Structure des Fichiers de Test**

```
test/
├── conftest.py              # ✅ Fixtures pytest + client h2c
├── requirements.txt         # ✅ Dépendances (pytest, h2, requests)
├── test_basic.py            # ✅ 9 tests d'infrastructure (Phase 1)
├── test_crud.py             # ⚪ À créer - 20 tests CRUD (Phase 3)
├── test_query_params.py     # ⚪ À créer - 15 tests paramètres (Phase 4)
├── test_rpc.py              # ⚪ À créer - 15 tests RPC/Actions (Phase 5)
├── test_notifications.py    # ⚪ À créer - 10 tests notifications (Phase 5)
├── test_nmda.py             # ⚪ À créer - 10 tests NMDA (Phase 6)
├── test_errors.py           # ⚪ À créer - 12 tests erreurs (Phase 7)
├── test_security.py         # ⚪ À créer - 10 tests sécurité (Phase 8)
└── test_performance.py      # ⚪ À créer - 10 tests performance (Phase 9)
```

---

## 📚 **Documentation Associée**

| Type | Document | Emplacement |
|------|----------|------------|
| RFC | RFC 8040 - RESTCONF Protocol | `doc/rfc/rfc8040.txt` |
| RFC | RFC 8527 - NMDA | `doc/rfc/rfc8527.txt` |
| RFC | RFC 6415 - WebSocket | `doc/rfc/rfc6415.txt` |
| Module | oven.yang (exemple simple) | `doc/test/modules/oven.yang` |
| Module | restconf-test.yang (complet) | `doc/test/modules/restconf-test.yang` |
| Stratégie | Stratégie de test complète | `doc/test/STRATEGY.md` |
| Cas | Tests par domaine | `doc/test/cases/*/` |
| Matrices | Conformité RFC | `doc/test/matrices/` |
| Scénarios | End-to-end | `doc/test/scenarios/` |
| Exemples | Exemple oven.yang | `doc/test/examples/oven/` |

---

## 🚀 **Comment Contribuer**

### 1. **Choisir une tâche**
- Consulter ce document pour identifier les tests non implémentés
- Vérifier les priorités dans le tableau de bord

### 2. **Créer un nouveau fichier de test**
```bash
# Exemple pour les tests CRUD
touch test/test_crud.py
```

### 3. **Utiliser les fixtures existantes**
```python
# Dans test/test_crud.py
from conftest import client, server_process, base_url

class TestCRUD:
    def test_get_data_root(self, server_process, client):
        resp = client.get("/restconf/data")
        assert resp.status_code in (200, 401, 403)
```

### 4. **Vérifier que restconf-test.yang est chargé**
```python
# Au début de chaque test de qualification
import json

def check_module_loaded(client):
    """Vérifie que restconf-test.yang est chargé."""
    resp = client.get("/restconf/data/rt:restconf-test")
    if resp.status_code == 404:
        pytest.skip("Module restconf-test.yang non chargé")
    assert resp.status_code == 200
```

### 5. **Exécuter les tests**
```bash
# Tous les tests
pytest test/ -v

# Tests spécifiques
pytest test/test_crud.py -v

# Un test spécifique
pytest test/test_crud.py::TestCRUD::test_get_data_root -v
```

---

## 📈 **Priorités**

### 🔴 **Haute Priorité** (Blocants pour la conformité)
1. **Phase 3 - CRUD** : Sans CRUD, pas de validation fonctionnelle
2. **Phase 7 - Erreurs** : Compléter les tests d'erreurs
3. **Phase 5 - RPC** : Fonctionnalités critiques

### 🟡 **Moyenne Priorité** (Importants pour la conformité)
1. **Phase 4 - Query Parameters** : Fonctionnalités avancées
2. **Phase 6 - NMDA** : Support multi-datastore
3. **Phase 2 - Sécurité** : Authentification de base

### 🟢 **Basse Priorité** (Nice to have)
1. **Phase 8 - Sécurité Avancée** : NACM, HTTPS
2. **Phase 9 - Performance** : Tests de charge
3. **Phase 10 - CI/CD** : Intégration continue

---

## 📞 **Support**

- **Questions sur un test spécifique** : Voir `doc/test/cases/*/`
- **Problèmes avec le client h2c** : Voir `test/conftest.py`
- **Documentation RFC** : Voir `doc/rfc/`
- **Stratégie de test** : Voir `doc/test/STRATEGY.md`

---

## 🎯 **Prochaines Étapes Recommandées**

1. **Implémenter les tests CRUD (Phase 3)** - Priorité maximale
2. **Compléter les tests d'erreurs (Phase 7)** - 12 tests restants
3. **Créer les tests RPC (Phase 5)** - 25 tests
4. **Intégrer avec CTest (Phase 10)** - Pour CI/CD

---

*Document généré le : 2026-07-09*
*Dernière mise à jour : Voir l'historique Git*
