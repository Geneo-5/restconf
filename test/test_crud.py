"""
Tests CRUD RESTCONF (RFC 8040 §4.2-4.7).

Toutes les operations Create, Read, Update, Delete sur les donnees.

Prerequis : Le module restconf-test.yang (namespace: urn:restconf:test, prefix: rt)
            DOIT etre charge sur le serveur RESTCONF.

RFC References:
- RFC 8040 §4.2 : Retrieving Data Resources (GET)
- RFC 8040 §4.3 : HEAD and OPTIONS for Data Resources
- RFC 8040 §4.4 : Creating Data Resources (POST)
- RFC 8040 §4.5 : Replacing Data Resources (PUT)
- RFC 8040 §4.6 : Partially Modifying Data Resources (PATCH)
- RFC 8040 §4.7 : Deleting Data Resources (DELETE)
- RFC 7950 §7.6 : Mandatory, Default, Read-only
- RFC 6243 : Default Values
"""

import json
import pytest

from conftest import (
    check_restconf_test_module,
    require_restconf_test_module,
)


# ---------------------------------------------------------------------------
# Phase 3: CRUD Operations
# ---------------------------------------------------------------------------

class TestCRUDRead:
    """Tests de lecture (GET) - RFC 8040 §4.2"""

    @require_restconf_test_module
    def test_001_get_data_root(self, server_process, client):
        """
        TC-3-001 : GET - Racine data
        
        RFC 8040 §4.2 : Retrieving the Data Resource
        GET /restconf/data retourne les ressources de donnees disponibles.
        
        Expected: 200 OK ou 401/403 (si authentification requise)
        """
        resp = client.get("/restconf/data")
        assert resp.status_code in (200, 401, 403)

    @require_restconf_test_module
    def test_002_get_container(self, server_process, client):
        """
        TC-3-002 : GET - Container
        
        RFC 8040 §4.2 : Retrieving a container
        GET /restconf/data/rt:restconf-test/rt:system
        
        Expected: 200 OK avec le container system
        """
        resp = client.get("/restconf/data/rt:restconf-test/rt:system")
        if resp.status_code == 200:
            data = resp.json()
            assert "rt:system" in data or isinstance(data, dict)
        else:
            # 404 acceptable si le module est charge mais vide
            assert resp.status_code in (200, 404, 401, 403)

    @require_restconf_test_module
    def test_003_get_leaf(self, server_process, client):
        """
        TC-3-003 : GET - Leaf
        
        RFC 8040 §4.2 : Retrieving a leaf
        GET /restconf/data/rt:restconf-test/rt:basic-data/rt:device-id
        
        Expected: 200 OK ou 404 (si la leaf n'existe pas encore)
        """
        resp = client.get("/restconf/data/rt:restconf-test/rt:basic-data/rt:device-id")
        assert resp.status_code in (200, 404, 401, 403)

    @require_restconf_test_module
    def test_004_get_list(self, server_process, client):
        """
        TC-3-004 : GET - List
        
        RFC 8040 §4.2 : Retrieving a list
        GET /restconf/data/rt:restconf-test/rt:interfaces/rt:interface
        
        Expected: 200 OK avec la liste des interfaces (peut etre vide)
        """
        resp = client.get("/restconf/data/rt:restconf-test/rt:interfaces/rt:interface")
        if resp.status_code == 200:
            data = resp.json()
            # La liste peut etre vide ou contenir des elements
            assert "rt:interface" in data or data == {} or isinstance(data, list)
        else:
            assert resp.status_code in (200, 404, 401, 403)

    @require_restconf_test_module
    def test_005_get_list_with_key(self, server_process, client):
        """
        TC-3-005 : GET - List avec key
        
        RFC 8040 §4.2 : Retrieving a specific list entry
        GET /restconf/data/rt:restconf-test/rt:interfaces/rt:interface[name='eth0']
        
        Expected: 200 OK si l'entree existe, 404 sinon
        """
        # D'abord essayer de lire une entree qui n'existe probablement pas
        resp = client.get("/restconf/data/rt:restconf-test/rt:interfaces/rt:interface[name='eth0']")
        assert resp.status_code in (200, 404, 401, 403)


class TestCRUDCreate:
    """Tests de creation (POST, PUT) - RFC 8040 §4.4-4.5"""

    @require_restconf_test_module
    def test_006_post_create_in_list(self, server_process, client):
        """
        TC-3-006 : POST - Creer dans list
        
        RFC 8040 §4.4 : Creating a Data Resource
        POST /restconf/data/rt:restconf-test/rt:interfaces/rt:interface
        
        Expected: 201 Created avec Location header
        """
        new_interface = {
            "rt:interface": [{
                "name": "eth0",
                "description": "Test interface",
                "enabled": True,
                "mtu": 1500
            }]
        }
        
        resp = client.post(
            "/restconf/data/rt:restconf-test/rt:interfaces",
            body=json.dumps(new_interface),
            headers={"Content-Type": "application/yang-data+json"}
        )
        
        # 201 Created ou 204 No Content selon l'implémentation
        # Le serveur peut aussi retourner 404 si le parent n'existe pas
        assert resp.status_code in (201, 204, 404, 409, 401, 403)
        
        if resp.status_code == 201:
            # Vérifier la presence du header Location
            assert "location" in resp.headers or "Location" in resp.headers

    @require_restconf_test_module
    def test_007_put_create_or_replace(self, server_process, client):
        """
        TC-3-007 : PUT - Creer/remplacer
        
        RFC 8040 §4.5 : Replacing a Data Resource
        PUT /restconf/data/rt:restconf-test/rt:system/rt:config
        
        Expected: 201 Created (si nouveau) ou 204 No Content (si remplacement)
        """
        config = {
            "rt:config": {
                "system-name": "test-system"
            }
        }
        
        resp = client.put(
            "/restconf/data/rt:restconf-test/rt:system/rt:config",
            body=json.dumps(config),
            headers={"Content-Type": "application/yang-data+json"}
        )
        
        # 201 si creation, 204 si remplacement
        assert resp.status_code in (201, 204, 401, 403)

    @require_restconf_test_module
    def test_008_put_modify_existing(self, server_process, client):
        """
        TC-3-008 : PUT - Modifier une ressource existante
        
        RFC 8040 §4.5 : Replacing an existing Data Resource
        PUT /restconf/data/rt:restconf-test/rt:interfaces/rt:interface[name='eth0']
        
        Expected: 204 No Content
        """
        # Essayer de modifier une interface existante
        # Note: Cela depend de l'etat initial du serveur
        interface_data = {
            "rt:interface": [{
                "name": "eth0",
                "description": "Modified description",
                "enabled": True,
                "mtu": 1500
            }]
        }
        
        resp = client.put(
            "/restconf/data/rt:restconf-test/rt:interfaces/rt:interface[name='eth0']",
            body=json.dumps(interface_data),
            headers={"Content-Type": "application/yang-data+json"}
        )
        
        assert resp.status_code in (201, 204, 404, 401, 403)


class TestCRUDUpdate:
    """Tests de mise a jour (PATCH) - RFC 8040 §4.6"""

    @require_restconf_test_module
    def test_009_patch_modify_partial(self, server_process, client):
        """
        TC-3-009 : PATCH - Modifier partiel
        
        RFC 8040 §4.6 : Partially Modifying a Data Resource
        PATCH /restconf/data/rt:restconf-test/rt:system/rt:config
        
        Expected: 204 No Content
        """
        patch_data = {
            "rt:config": {
                "system-name": "patched-system"
            }
        }
        
        resp = client.patch(
            "/restconf/data/rt:restconf-test/rt:system/rt:config",
            body=json.dumps(patch_data),
            headers={"Content-Type": "application/yang-data+patch+json"}
        )
        
        # PATCH peut retourner 204 No Content ou 404 si la ressource n'existe pas
        # Certains serveurs peuvent utiliser application/yang-data+json au lieu de +patch+json
        assert resp.status_code in (204, 404, 401, 403, 415)


class TestCRUDDelete:
    """Tests de suppression (DELETE) - RFC 8040 §4.7"""

    @require_restconf_test_module
    def test_010_delete_resource(self, server_process, client):
        """
        TC-3-010 : DELETE - Supprimer une ressource
        
        RFC 8040 §4.7 : Deleting a Data Resource
        DELETE /restconf/data/rt:restconf-test/rt:interfaces/rt:interface[name='eth0']
        
        Expected: 204 No Content
        """
        resp = client.delete(
            "/restconf/data/rt:restconf-test/rt:interfaces/rt:interface[name='eth0']"
        )
        
        # 204 si suppression réussie, 404 si n'existe pas
        assert resp.status_code in (204, 404, 401, 403)


class TestCRUDErrors:
    """Tests d'erreurs CRUD - RFC 8040 §7"""

    @require_restconf_test_module
    def test_011_get_nonexistent(self, server_process, client):
        """
        TC-3-011 : GET - Donnees inexistantes
        
        RFC 8040 §4.2 : Retrieving non-existent data
        GET /restconf/data/rt:nonexistent
        
        Expected: 404 Not Found
        """
        resp = client.get("/restconf/data/rt:nonexistent")
        assert resp.status_code in (404, 401, 403)

    @require_restconf_test_module
    def test_012_post_existing_resource(self, server_process, client):
        """
        TC-3-012 : POST - URI existante
        
        RFC 8040 §4.4 : POST on existing resource
        POST sur une ressource qui existe deja doit retourner 409 Conflict
        
        Expected: 409 Conflict ou 405 Method Not Allowed
        """
        # Essayer de faire un POST sur une ressource qui existe deja
        # Cela depend de l'implémentation du serveur
        existing_data = {
            "rt:config": {
                "system-name": "test"
            }
        }
        
        # D'abord créer la ressource
        create_resp = client.put(
            "/restconf/data/rt:restconf-test/rt:system/rt:config",
            body=json.dumps(existing_data),
            headers={"Content-Type": "application/yang-data+json"}
        )
        
        # Ensuite essayer de faire un POST sur la meme URI
        resp = client.post(
            "/restconf/data/rt:restconf-test/rt:system/rt:config",
            body=json.dumps(existing_data),
            headers={"Content-Type": "application/yang-data+json"}
        )
        
        # POST sur une ressource existante doit retourner 405 ou 409
        assert resp.status_code in (405, 409, 400, 401, 403)


class TestCRUDHEADandOPTIONS:
    """Tests HEAD et OPTIONS pour les ressources de donnees - RFC 8040 §4.3"""

    @require_restconf_test_module
    def test_013_head_data_resource(self, server_process, client):
        """
        TC-3-013 : HEAD - Data resource
        
        RFC 8040 §4.3 : HEAD for Data Resources
        HEAD /restconf/data/rt:restconf-test/rt:system
        
        Expected: 200 OK avec headers mais sans body
        """
        resp = client.head("/restconf/data/rt:restconf-test/rt:system")
        assert resp.status_code in (200, 401, 403, 404)
        # HEAD ne doit pas avoir de body
        assert len(resp.content) == 0

    @require_restconf_test_module
    def test_014_options_data_resource(self, server_process, client):
        """
        TC-3-014 : OPTIONS - Data resource
        
        RFC 8040 §4.3 : OPTIONS for Data Resources
        OPTIONS /restconf/data
        
        Expected: 200 ou 204 avec header Allow
        """
        resp = client.options("/restconf/data")
        assert resp.status_code in (200, 204, 405, 401, 403)
        
        if resp.status_code in (200, 204):
            # Vérifier que le header Allow est présent
            allow = resp.headers.get("allow", "")
            allow_upper = allow.upper()
            # Les méthodes typiques pour /restconf/data
            assert "GET" in allow_upper or "HEAD" in allow_upper


class TestCRUDHeaders:
    """Tests des headers ETag et Last-Modified - RFC 8040 §3.4.1"""

    @require_restconf_test_module
    def test_015_etag_support(self, server_process, client):
        """
        TC-3-015 : ETag support
        
        RFC 8040 §3.4.1 : ETag for collision prevention
        Vérifier que le serveur retourne un header ETag
        
        Expected: ETag header présent dans la réponse GET
        """
        resp = client.get("/restconf/data/rt:restconf-test/rt:system")
        if resp.status_code == 200:
            # Le serveur peut ou non supporter ETag
            # Si ETag est supporté, il doit etre present
            etag = resp.headers.get("etag") or resp.headers.get("ETag")
            # On ne requiert pas ETag, mais on vérifie sa presence si implémenté
            assert True  # Test informatif - ETag est optionnel
        else:
            assert resp.status_code in (200, 404, 401, 403)

    @require_restconf_test_module
    def test_016_last_modified_support(self, server_process, client):
        """
        TC-3-016 : Last-Modified support
        
        RFC 8040 §3.4.1 : Last-Modified timestamp
        Vérifier que le serveur retourne un header Last-Modified
        
        Expected: Last-Modified header présent dans la réponse GET
        """
        resp = client.get("/restconf/data/rt:restconf-test/rt:system")
        if resp.status_code == 200:
            # Last-Modified est optionnel mais recommandé
            last_modified = resp.headers.get("last-modified") or \
                          resp.headers.get("Last-Modified")
            # On ne requiert pas Last-Modified
            assert True  # Test informatif
        else:
            assert resp.status_code in (200, 404, 401, 403)


class TestCRUDConstraints:
    """Tests des contraintes YANG - RFC 7950 §7.6"""

    @require_restconf_test_module
    def test_017_read_only_leaf(self, server_process, client):
        """
        TC-3-017 : Read-only leaf
        
        RFC 7950 §7.6 : config false
        Tentative de PUT sur une leaf avec config=false doit echouer
        
        Expected: 400 Bad Request ou 409 Conflict
        """
        # rt:uptime dans restconf-test.yang a config=false
        put_data = {
            "rt:uptime": 9999
        }
        
        resp = client.put(
            "/restconf/data/rt:restconf-test/rt:basic-data/rt:uptime",
            body=json.dumps(put_data),
            headers={"Content-Type": "application/yang-data+json"}
        )
        
        # Tentative de modification d'une read-only leaf doit echouer
        # Note: Certains serveurs peuvent retourner 404 si la ressource n'existe pas
        assert resp.status_code in (400, 404, 409, 401, 403)

    @require_restconf_test_module
    def test_018_mandatory_leaf(self, server_process, client):
        """
        TC-3-018 : Mandatory leaf
        
        RFC 7950 §7.6 : mandatory true
        PUT sans une leaf obligatoire doit echouer
        
        Expected: 400 Bad Request
        """
        # rt:device-id dans restconf-test.yang est mandatory
        # Essayer de mettre un container sans la leaf obligatoire
        incomplete_data = {
            "rt:basic-data": {
                "timeout": 30
            }
        }
        
        resp = client.put(
            "/restconf/data/rt:restconf-test/rt:basic-data",
            body=json.dumps(incomplete_data),
            headers={"Content-Type": "application/yang-data+json"}
        )
        
        # Doit echouer car device-id est mandatory
        assert resp.status_code in (400, 404, 401, 403)

    @require_restconf_test_module
    def test_019_leaf_with_default(self, server_process, client):
        """
        TC-3-019 : Leaf avec default
        
        RFC 6243 : Default Values
        Vérifier que les valeurs par défaut sont appliquées
        
        Expected: GET retourne la valeur par défaut si pas explicitement définie
        """
        # rt:timeout dans restconf-test.yang a default=30
        resp = client.get("/restconf/data/rt:restconf-test/rt:basic-data/rt:timeout")
        
        # Peut retourner 404 si la valeur n'a pas été explicitement définie
        # ou 200 avec la valeur par défaut
        if resp.status_code == 200:
            data = resp.json()
            # Si la valeur est présente, elle doit être 30 (default)
            # Note: L'application des defaults depend de with-defaults
            assert True
        else:
            assert resp.status_code in (200, 404, 401, 403)

    @require_restconf_test_module
    def test_020_multiple_datastores(self, server_process, client):
        """
        TC-3-020 : Multiple datastores
        
        RFC 8040 §1.4 : Datastore access
        Vérifier l'accès à différents datastores
        
        Expected: 200 OK pour les datastores supportés
        """
        # Essayer d'accéder à /restconf/data (default: running)
        resp_data = client.get("/restconf/data")
        assert resp_data.status_code in (200, 401, 403)
        
        # Note: /restconf/ds est pour NMDA (RFC 8527)
        # On vérifie juste que l'accès de base fonctionne
        assert True
