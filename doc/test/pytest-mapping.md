# Mapping Pytest - Cas de Test RESTCONF

## ⚠️ Module YANG de référence

**Tous les tests pytest DOIVENT utiliser le module `restconf-test.yang`** (namespace: `urn:restconf:test`, prefix: `rt`).

Vérifier que le module est chargé avant d'exécuter les tests :
```python
import requests

def test_module_loaded():
    response = requests.get("http://server/restconf/data/rt:restconf-test")
    assert response.status_code == 200
    assert "rt:restconf-test" in response.text
```

---

## Structure des Tests Pytest

```
tests/
├── conftest.py                    # Fixtures communes
├── test_infrastructure/           # Phase 2 - Infrastructure
│   ├── test_host_metadata.py      # TC-2-001 à TC-2-004
│   ├── test_https.py              # TC-2-006 à TC-2-012
│   └── test_api_root.py           # TC-2-013 à TC-2-020
├── test_crud/                     # Phase 3 - CRUD
│   ├── test_get.py                 # TC-3-001 à TC-3-008, TC-3-036 à TC-3-038
│   ├── test_post.py               # TC-3-011 à TC-3-017
│   ├── test_put.py                 # TC-3-018 à TC-3-021
│   ├── test_patch.py              # TC-3-022 à TC-3-026
│   └── test_delete.py             # TC-3-027 à TC-3-031
├── test_parameters/               # Phase 4 - Query Parameters
│   ├── test_content.py            # TC-4-001 à TC-4-005
│   ├── test_depth.py              # TC-4-006 à TC-4-013
│   ├── test_fields.py             # TC-4-014 à TC-4-025
│   ├── test_filter.py             # TC-4-021 à TC-4-025
│   ├── test_insert.py             # TC-4-031 à TC-4-036
│   ├── test_point.py              # TC-4-037 à TC-4-039
│   └── test_combinations.py       # TC-4-043 à TC-4-055
├── test_rpc/                      # Phase 5 - RPC & Actions
│   ├── test_rpc.py                # TC-5-001 à TC-5-014
│   ├── test_actions.py            # TC-5-015 à TC-5-019
│   └── test_notifications.py      # TC-5-020 à TC-5-040
├── test_nmda/                     # Phase 6 - NMDA
│   ├── test_datastores.py         # TC-6-001 à TC-6-016
│   └── test_with_origin.py        # TC-6-024 à TC-6-032
├── test_errors/                   # Phase 7 - Error Handling
│   ├── test_http_codes.py         # TC-7-001 à TC-7-013
│   ├── test_yang_constraints.py   # TC-7-014 à TC-7-050
│   └── test_error_messages.py     # TC-7-062 à TC-7-067
├── test_security/                 # Phase 8 - Security
│   ├── test_authentication.py     # TC-8-001 à TC-8-013
│   └── test_authorization.py      # TC-8-014 à TC-8-020
└── test_performance/              # Phase 9 - Performance
    ├── test_load.py               # TC-9-001 à TC-9-010
    └── test_concurrency.py        # TC-9-011 à TC-9-020
```

---

## Mapping Complet : Cas de Test → Pytest

### Phase 2 - Infrastructure (TC-2-001 à TC-2-029)

| ID Test | Description | Fichier Pytest | Fonction | Module YANG |
|---------|-------------|----------------|----------|-------------|
| TC-2-001 | Host Metadata | test_host_metadata.py | test_host_metadata_exists | N/A |
| TC-2-002 | Lien RESTCONF | test_host_metadata.py | test_restconf_link_present | N/A |
| TC-2-003 | URI RESTCONF valide | test_host_metadata.py | test_restconf_uri_valid | N/A |
| TC-2-004 | Accès Root RESTCONF | test_api_root.py | test_api_root_accessible | N/A |
| TC-2-005 | Ressource inconnue | test_api_root.py | test_unknown_resource | N/A |
| TC-2-006 | Refus HTTP | test_https.py | test_http_rejected | N/A |
| TC-2-007 | Certificat TLS | test_https.py | test_tls_certificate | N/A |
| TC-2-008 | Version TLS | test_https.py | test_tls_version | N/A |
| TC-2-009 | Authentifié | test_authentication.py | test_authenticated_access | N/A |
| TC-2-010 | Utilisateur inconnu | test_authentication.py | test_unknown_user | N/A |
| TC-2-011 | Mot de passe incorrect | test_authentication.py | test_wrong_password | N/A |
| TC-2-012 | Authentification absente | test_authentication.py | test_missing_auth | N/A |
| TC-2-013 | Authentification invalide | test_authentication.py | test_invalid_auth | N/A |
| TC-2-014 | JSON Accept | test_content_negotiation.py | test_json_accept | N/A |
| TC-2-015 | XML Accept | test_content_negotiation.py | test_xml_accept | N/A |
| TC-2-016 | Accept invalide | test_content_negotiation.py | test_invalid_accept | N/A |

### Phase 3 - CRUD (TC-3-001 à TC-3-050) - Module rt:restconf-test

| ID Test | Description | Fichier | Fonction | URI |
|---------|-------------|--------|----------|-----|
| TC-3-001 | Lecture /data | test_get.py | test_get_data_root | /restconf/data |
| TC-3-002 | Lecture container | test_get.py | test_get_container | /restconf/data/rt:restconf-test/rt:system |
| TC-3-003 | Presence Container | test_get.py | test_get_presence_container | /restconf/data/rt:restconf-test/rt:system/rt:config |
| TC-3-004 | Lecture leaf | test_get.py | test_get_leaf | /restconf/data/rt:restconf-test/rt:basic-data/rt:device-id |
| TC-3-005 | Lecture list | test_get.py | test_get_list | /restconf/data/rt:restconf-test/rt:interfaces/rt:interface |
| TC-3-006 | Lecture entrée list | test_get.py | test_get_list_entry | /restconf/data/rt:restconf-test/rt:interfaces/rt:interface=eth0 |
| TC-3-007 | Clé inexistante | test_get.py | test_get_nonexistent_key | /restconf/data/rt:restconf-test/rt:interfaces/rt:interface=unknown |
| TC-3-008 | Leaf-list | test_get.py | test_get_leaf_list | /restconf/data/rt:restconf-test/rt:access-control/rt:allowed-ips |
| TC-3-009 | Anydata | test_get.py | test_get_anydata | /restconf/data/rt:restconf-test/rt:custom-data/rt:vendor-specific |
| TC-3-010 | Anyxml | test_get.py | test_get_anyxml | /restconf/data/rt:restconf-test/rt:custom-data/rt:legacy-config |

---

## Exemple de Code Pytest

```python
# test_crud/test_get.py
import pytest
import requests
from conftest import RESTCONF_BASE_URL, AUTH_HEADERS

class TestGetOperations:
    """Tests pour les opérations GET (TC-3-001 à TC-3-010)"""
    
    def test_get_data_root(self):
        """TC-3-001 : Lecture de la racine des données"""
        url = f"{RESTCONF_BASE_URL}/data"
        response = requests.get(url, headers=AUTH_HEADERS)
        
        assert response.status_code == 200
        assert "rt:restconf-test" in response.text
        
    def test_get_container(self):
        """TC-3-002 : Lecture d'un container"""
        url = f"{RESTCONF_BASE_URL}/data/rt:restconf-test/rt:system"
        response = requests.get(url, headers=AUTH_HEADERS)
        
        assert response.status_code == 200
        # Vérifier la structure du container
        
    def test_get_list(self):
        """TC-3-005 : Lecture d'une liste"""
        url = f"{RESTCONF_BASE_URL}/data/rt:restconf-test/rt:interfaces/rt:interface"
        response = requests.get(url, headers=AUTH_HEADERS)
        
        assert response.status_code == 200
        # Vérifier que la liste est retournée
        
    def test_get_presence_container(self):
        """TC-3-003 : Lecture d'un presence container"""
        url = f"{RESTCONF_BASE_URL}/data/rt:restconf-test/rt:system/rt:config"
        response = requests.get(url, headers=AUTH_HEADERS)
        
        # Un presence container peut retourner 404 s'il n'est pas présent
        # ou 200 avec les données
        assert response.status_code in [200, 404]

# test_parameters/test_content.py
class TestContentParameter:
    """Tests pour le paramètre content (TC-4-001 à TC-4-005)"""
    
    def test_content_config(self):
        """TC-4-001 : content=config"""
        url = f"{RESTCONF_BASE_URL}/data/rt:restconf-test?content=config"
        response = requests.get(url, headers=AUTH_HEADERS)
        
        assert response.status_code == 200
        # Vérifier qu'aucune donnée config false n'est présente
        assert "rt:uptime" not in response.text
        
    def test_content_invalid(self):
        """TC-4-004 : Valeur content invalide"""
        url = f"{RESTCONF_BASE_URL}/data/rt:restconf-test?content=invalid"
        response = requests.get(url, headers=AUTH_HEADERS)
        
        assert response.status_code == 400

# test_rpc/test_rpc.py
class TestRpcOperations:
    """Tests pour les RPC (TC-5-001 à TC-5-014)"""
    
    def test_rpc_discovery(self):
        """TC-5-001 : Présence de /operations"""
        url = f"{RESTCONF_BASE_URL}/operations"
        response = requests.get(url, headers=AUTH_HEADERS)
        
        assert response.status_code == 200
        assert "rt:get-system-status" in response.text
        assert "rt:configure-device" in response.text
        
    def test_rpc_no_param(self):
        """TC-5-004 : RPC sans paramètres"""
        url = f"{RESTCONF_BASE_URL}/operations/rt:get-system-status"
        response = requests.post(url, headers=AUTH_HEADERS)
        
        assert response.status_code == 200
        assert "status" in response.json().get("rt:output", {})
        
    def test_rpc_with_param(self):
        """TC-5-005 : RPC avec paramètres"""
        url = f"{RESTCONF_BASE_URL}/operations/rt:configure-device"
        payload = {
            "rt:input": {
                "device-name": "test-device",
                "enable": True
            }
        }
        response = requests.post(url, json=payload, headers=AUTH_HEADERS)
        
        assert response.status_code == 200
        assert "result" in response.json().get("rt:output", {})
```

---

## Fixtures Recommandées (conftest.py)

```python
# conftest.py
import pytest
import requests

# Configuration
RESTCONF_BASE_URL = "http://localhost:8080/restconf"
AUTH_HEADERS = {
    "Authorization": "Bearer YOUR_TOKEN",
    "Accept": "application/yang-data+json",
    "Content-Type": "application/yang-data+json"
}

@pytest.fixture(scope="session")
def restconf_client():
    """Client RESTCONF pour la session de test"""
    return requests.Session()

@pytest.fixture(autouse=True)
def verify_module_loaded(restconf_client):
    """Vérifie que restconf-test.yang est chargé avant chaque test"""
    response = restconf_client.get(
        f"{RESTCONF_BASE_URL}/data/rt:restconf-test",
        headers=AUTH_HEADERS
    )
    assert response.status_code == 200, "Module rt:restconf-test non chargé"

@pytest.fixture
def cleanup_test_data(restconf_client):
    """Nettoie les données de test après chaque test"""
    yield
    # Supprimer les données créées pendant le test
    # À implémenter selon les besoins
```

---

## Intégration avec CTest

Pour intégrer les tests pytest avec CTest (comme mentionné dans la ROADMAP) :

```cmake
# Dans CMakeLists.txt
enable_testing()

# Ajouter les tests pytest
add_test(
    NAME restconf_tests
    COMMAND pytest
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests
    COMMENT "Execution des tests RESTCONF"
)
```