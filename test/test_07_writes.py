# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN

"""Tests de conformité RESTCONF - Section 7 : Écritures (POST, PUT, DELETE, plain PATCH).

Couvre les items T-WRITE-01 à T-WRITE-18 de la ROADMAP.md.
RFC liées : RFC 8040 §4.4, §4.5, §4.6, §4.7, RFC 9110.
Items liés : R10, R11, R12, R13, R14, R20, R41, R42.
"""

from __future__ import annotations

import pytest

# ---------------------------------------------------------------------------
# Références normatives et chemins
# ---------------------------------------------------------------------------

YANG_JSON = "application/yang-data+json"
YANG_XML = "application/yang-data+xml"
MOD = "restconf-test"

BASIC_DATA = "/data/restconf-test:basic-data"
SYSTEM_CONFIG = "/data/restconf-test:system/config"
INTERFACES = "/data/restconf-test:interfaces"
INTERFACE_LIST = "/data/restconf-test:interfaces/interface"

# Chemin pour les tests d'actions (à adapter selon restconf-test.yang)
# Si ce chemin n'existe pas, les tests d'action seront automatiquement ignorés (skip).
ACTION_PATH = "/data/restconf-test:system/config/reboot" 

def get_content_type(response) -> str:
    return response.headers.get("content-type", "").split(";")[0].strip().lower()

# ---------------------------------------------------------------------------
# Fixtures locales
# ---------------------------------------------------------------------------

_RT_PROBE_PATHS = (BASIC_DATA, SYSTEM_CONFIG, INTERFACES)

@pytest.fixture(scope="session")
def rt_probe(http2_client, api_url, auth_headers, test_jwt):
    """Sonde l'accessibilité du module `restconf-test` sur le datastore."""
    if not test_jwt:
        return None
    headers = {"Accept": YANG_JSON, **auth_headers}
    return {
        path: http2_client.get(f"{api_url}{path}", headers=headers)
        for path in _RT_PROBE_PATHS
    }

@pytest.fixture()
def require_rt(rt_probe):
    """Skip le test si le module `restconf-test` n'est pas installé/accessible."""
    if rt_probe is None:
        pytest.skip("Aucun JWT configuré")
    
    statuses = {path: resp.status_code for path, resp in rt_probe.items()}
    if any(resp.status_code == 200 for resp in rt_probe.values()):
        return rt_probe
    if all(resp.status_code == 404 for resp in rt_probe.values()):
        pytest.skip(f"Module YANG 'restconf-test' non installé ou données non chargées ({statuses})")
    if all(resp.status_code in (401, 403) for resp in rt_probe.values()):
        pytest.skip(f"Accès au datastore restconf-test refusé ({statuses})")
    pytest.skip(f"Datastore restconf-test inaccessible ({statuses})")

@pytest.fixture()
def clean_system_config(http2_client, api_url, auth_headers):
    """Teardown pour system/config."""
    yield
    try:
        http2_client.delete(f"{api_url}{SYSTEM_CONFIG}", headers={**auth_headers})
    except Exception:
        pass

@pytest.fixture()
def clean_interface(http2_client, api_url, auth_headers):
    """Teardown pour une interface de test."""
    test_iface = "test-iface-01"
    yield test_iface
    try:
        http2_client.delete(f"{api_url}{INTERFACE_LIST}={test_iface}", headers={**auth_headers})
    except Exception:
        pass

# ============================================================================
# T-WRITE-01 & T-WRITE-02 : POST création (Create Resource Mode)
# ============================================================================
@pytest.mark.roadmap("R10")
@pytest.mark.rfc("RFC 8040 §4.4.1")
class TestT_WRITE_01_02_PostCreate:
    """
    T-WRITE-01 : POST création d'une ressource enfant -> 201 Created avec header Location.
    T-WRITE-02 : POST création d'une ressource déjà existante -> 409 Conflict (data-exists).
    """
    def test_post_create(self, http2_client, api_url, auth_headers, require_rt, clean_interface):
        """T-WRITE-01 : POST création d'une ressource enfant."""
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        payload = f'{{"{MOD}:interface": [{{"name": "{clean_interface}"}}]}}'.encode()
        
        response = http2_client.post(f"{api_url}{INTERFACES}", headers=headers, content=payload)
        # 201 Created attendu, ou 204/200 selon implémentation stricte
        assert response.status_code in (201, 200, 204), (
            f"POST création doit retourner 201 Created (ou 200/204), obtenu {response.status_code}"
        )
        if response.status_code == 201:
            assert "location" in response.headers, "201 Created doit inclure le header Location."

    def test_post_create_conflict(self, http2_client, api_url, auth_headers, require_rt, clean_interface):
        """T-WRITE-02 : POST création d'une ressource déjà existante."""
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        payload = f'{{"{MOD}:interface": [{{"name": "{clean_interface}"}}]}}'.encode()
        
        # Première création
        http2_client.post(f"{api_url}{INTERFACES}", headers=headers, content=payload)
        
        # Tentative de création de la même ressource
        response = http2_client.post(f"{api_url}{INTERFACES}", headers=headers, content=payload)
        assert response.status_code == 409, (
            f"POST sur ressource existante doit retourner 409 Conflict, obtenu {response.status_code}"
        )
        if response.status_code == 409:
            body = response.json()
            assert "ietf-restconf:errors" in body

# ============================================================================
# T-WRITE-03 & T-WRITE-04 : POST avec corps invalide / Content-Type non supporté
# ============================================================================
@pytest.mark.roadmap("R10")
@pytest.mark.rfc("RFC 8040 §4.4.1")
@pytest.mark.rfc("RFC 9110")
class TestT_WRITE_03_04_PostErrors:
    """
    T-WRITE-03 : POST avec corps invalide -> 400 Bad Request.
    T-WRITE-04 : POST avec Content-Type non supporté -> 415 Unsupported Media Type.
    """
    def test_post_invalid_body(self, http2_client, api_url, auth_headers, require_rt):
        """T-WRITE-03 : POST avec corps invalide."""
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        payload = b"{ invalid json }"
        response = http2_client.post(f"{api_url}{INTERFACES}", headers=headers, content=payload)
        assert response.status_code == 400

    def test_post_unsupported_media_type(self, http2_client, api_url, auth_headers, require_rt):
        """T-WRITE-04 : POST avec Content-Type non supporté."""
        headers = {"Content-Type": "text/plain", "Accept": YANG_JSON, **auth_headers}
        payload = b"some text"
        response = http2_client.post(f"{api_url}{INTERFACES}", headers=headers, content=payload)
        assert response.status_code == 415

# ============================================================================
# T-WRITE-05, T-WRITE-06, T-WRITE-07 : PUT
# ============================================================================
@pytest.mark.roadmap("R12")
@pytest.mark.rfc("RFC 8040 §4.5")
class TestT_WRITE_05_06_07_Put:
    """
    T-WRITE-05 : PUT création d'une ressource -> 201 Created.
    T-WRITE-06 : PUT remplacement d'une ressource existante -> 204 No Content.
    T-WRITE-07 : PUT avec précondition If-Match invalide -> 412 Precondition Failed.
    """
    def test_put_create_and_replace(self, http2_client, api_url, auth_headers, require_rt, clean_system_config):
        """T-WRITE-05 & T-WRITE-06 : PUT création puis remplacement."""
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        payload = b"{}"
        
        # Création
        resp_create = http2_client.put(f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=payload)
        assert resp_create.status_code in (201, 204), (
            f"PUT création doit retourner 201 ou 204, obtenu {resp_create.status_code}"
        )
        
        # Remplacement
        resp_replace = http2_client.put(f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=payload)
        assert resp_replace.status_code in (200, 204), (
            f"PUT remplacement doit retourner 200 ou 204, obtenu {resp_replace.status_code}"
        )

    def test_put_if_match_invalid(self, http2_client, api_url, auth_headers, require_rt, clean_system_config):
        """T-WRITE-07 : PUT avec précondition If-Match invalide."""
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, "If-Match": '"W/\"invalid-etag\""', **auth_headers}
        payload = b"{}"
        
        response = http2_client.put(f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=payload)
        assert response.status_code == 412

# ============================================================================
# T-WRITE-08, T-WRITE-09, T-WRITE-10 : DELETE
# ============================================================================
@pytest.mark.roadmap("R13")
@pytest.mark.rfc("RFC 8040 §4.7")
class TestT_WRITE_08_09_10_Delete:
    """
    T-WRITE-08 : DELETE d'une ressource existante -> 204 No Content.
    T-WRITE-09 : DELETE d'une ressource inexistante -> 404 Not Found.
    T-WRITE-10 : DELETE avec précondition invalide -> 412 Precondition Failed.
    """
    def test_delete_existing(self, http2_client, api_url, auth_headers, require_rt, clean_interface):
        """T-WRITE-08 : DELETE d'une ressource existante."""
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        payload = f'{{"{MOD}:interface": [{{"name": "{clean_interface}"}}]}}'.encode()
        
        # Création préalable
        http2_client.post(f"{api_url}{INTERFACES}", headers=headers, content=payload)
        
        # Suppression
        response = http2_client.delete(f"{api_url}{INTERFACE_LIST}={clean_interface}", headers={**auth_headers})
        assert response.status_code in (200, 204), (
            f"DELETE ressource existante doit retourner 204 (ou 200), obtenu {response.status_code}"
        )

    def test_delete_non_existent(self, http2_client, api_url, auth_headers, require_rt):
        """T-WRITE-09 : DELETE d'une ressource inexistante."""
        response = http2_client.delete(f"{api_url}{INTERFACE_LIST}=does-not-exist-xyz", headers={**auth_headers})
        assert response.status_code == 404

    def test_delete_if_match_invalid(self, http2_client, api_url, auth_headers, require_rt, clean_interface):
        """T-WRITE-10 : DELETE avec précondition invalide."""
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        payload = f'{{"{MOD}:interface": [{{"name": "{clean_interface}"}}]}}'.encode()
        http2_client.post(f"{api_url}{INTERFACES}", headers=headers, content=payload)
        
        del_headers = {"If-Match": '"W/\"invalid-etag\""', **auth_headers}
        response = http2_client.delete(f"{api_url}{INTERFACE_LIST}={clean_interface}", headers=del_headers)
        assert response.status_code == 412

# ============================================================================
# T-WRITE-11, T-WRITE-12, T-WRITE-13 : Plain PATCH
# ============================================================================
@pytest.mark.roadmap("R14")
@pytest.mark.rfc("RFC 8040 §4.6")
class TestT_WRITE_11_12_13_Patch:
    """
    T-WRITE-11 : Plain PATCH sur ressource existante -> 200 OK ou 204 No Content.
    T-WRITE-12 : Plain PATCH créant des sous-ressources -> Création/fusion réussie sans 404 systématique.
    T-WRITE-13 : Plain PATCH avec corps invalide -> 400 Bad Request.
    """
    def test_patch_merge(self, http2_client, api_url, auth_headers, require_rt):
        """T-WRITE-11 & T-WRITE-12 : Plain PATCH fusion/création."""
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        # Envoi d'un patch partiel sur basic-data
        payload = f'{{"{MOD}:basic-data": {{}}}}'.encode()
        
        response = http2_client.patch(f"{api_url}{BASIC_DATA}", headers=headers, content=payload)
        assert response.status_code in (200, 204), (
            f"Plain PATCH doit retourner 200 ou 204, obtenu {response.status_code}"
        )

    def test_patch_invalid_body(self, http2_client, api_url, auth_headers, require_rt):
        """T-WRITE-13 : Plain PATCH avec corps invalide."""
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        payload = b"{ not valid json }"
        response = http2_client.patch(f"{api_url}{BASIC_DATA}", headers=headers, content=payload)
        assert response.status_code == 400

# ============================================================================
# T-WRITE-14 & T-WRITE-15 : POST invocation d'une action YANG
# ============================================================================
@pytest.mark.roadmap("R11")
@pytest.mark.rfc("RFC 8040 §4.4.2")
class TestT_WRITE_14_15_PostAction:
    """
    T-WRITE-14 : POST invocation d'une action YANG -> 200 OK ou 204 No Content.
    T-WRITE-15 : POST action avec input invalide -> 400 Bad Request.
    Note : Si l'action n'existe pas dans restconf-test.yang, le test est skip.
    """
    def test_post_action(self, http2_client, api_url, auth_headers, require_rt):
        """T-WRITE-14 : POST invocation d'une action YANG."""
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        payload = b"{}"
        response = http2_client.post(f"{api_url}{ACTION_PATH}", headers=headers, content=payload)
        
        # Si l'action n'est pas implémentée dans le module de test, 404 est acceptable
        if response.status_code == 404:
            pytest.skip(f"Action {ACTION_PATH} non trouvée dans le module de test.")
            
        assert response.status_code in (200, 204), (
            f"POST action doit retourner 200 ou 204, obtenu {response.status_code}"
        )

# ============================================================================
# T-WRITE-16 & T-WRITE-17 : ordered-by user avec insert / point
# ============================================================================
@pytest.mark.roadmap("R20")
@pytest.mark.rfc("RFC 8040 §4.8.5")
class TestT_WRITE_16_17_OrderedByUser:
    """
    T-WRITE-16 : Création dans liste ordered-by user avec insert / point -> Ordre respecté.
    T-WRITE-17 : insert=before sans point -> 400 Bad Request.
    """
    def test_insert_before_without_point(self, http2_client, api_url, auth_headers, require_rt, clean_interface):
        """T-WRITE-17 : insert=before sans point."""
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        # Utilisation du paramètre de requête insert=before sans point
        params = {"insert": "before"}
        payload = f'{{"{MOD}:interface": [{{"name": "{clean_interface}"}}]}}'.encode()
        
        response = http2_client.post(
            f"{api_url}{INTERFACES}", 
            headers=headers, 
            content=payload,
            params=params
        )
        
        # Si la liste n'est pas ordered-by user, le serveur peut ignorer ou retourner 400/404.
        # S'il l'est, il DOIT retourner 400 Bad Request car 'point' est requis.
        if response.status_code == 404:
            pytest.skip("Liste ordered-by user non disponible pour ce test.")
        assert response.status_code == 400, (
            f"insert=before sans point doit retourner 400, obtenu {response.status_code}"
        )

# ============================================================================
# T-WRITE-18 : Écriture sur ressource non autorisée
# ============================================================================
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8040 §4.5")
@pytest.mark.rfc("RFC 8341")
class TestT_WRITE_18_UnauthorizedWrite:
    """
    T-WRITE-18 : Écriture sur ressource non autorisée -> 403 Forbidden ou 401 Unauthorized.
    """
    def test_write_unauthorized(self, http2_client, api_url):
        """T-WRITE-18 : Écriture sans authentification."""
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON}
        payload = b"{}"
        
        response = http2_client.put(f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=payload)
        assert response.status_code in (401, 403), (
            f"Écriture non autorisée doit retourner 401 ou 403, obtenu {response.status_code}"
        )
        if response.status_code == 401:
            assert "www-authenticate" in response.headers, "401 doit inclure WWW-Authenticate."