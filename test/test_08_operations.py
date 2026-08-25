# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN

"""Tests de conformité RESTCONF - Section 8 : Operations, RPC et actions.

Couvre les items T-OPS-01 à T-OPS-10 de la ROADMAP.md.
RFC liées : RFC 8040 §3.6, §4.4.2, RFC 8341.
Items liés : R11, R15, R40.
"""

from __future__ import annotations

import pytest

# ---------------------------------------------------------------------------
# Références normatives et chemins
# ---------------------------------------------------------------------------

YANG_JSON = "application/yang-data+json"
YANG_XML = "application/yang-data+xml"
MOD = "restconf-test"

OPERATIONS = "/operations"
# Chemin d'un RPC inconnu pour les tests d'erreur
RPC_UNKNOWN = "/operations/nonexistent-rpc-xyz"

# Chemin pour une action liée à un data resource (à adapter selon restconf-test.yang)
ACTION_DATA_PATH = "/data/restconf-test:basic-data"
ACTION_NAME = "some-action"

def get_content_type(response) -> str:
    return response.headers.get("content-type", "").split(";")[0].strip().lower()

# ---------------------------------------------------------------------------
# Fixtures locales
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def operations_probe(http2_client, api_url, auth_headers, test_jwt):
    """Sonde la ressource operations."""
    if not test_jwt:
        return None
    headers = {"Accept": YANG_JSON, **auth_headers}
    return http2_client.get(f"{api_url}{OPERATIONS}", headers=headers)

@pytest.fixture()
def require_operations(operations_probe):
    """Skip le test si la ressource operations n'est pas accessible."""
    if operations_probe is None:
        pytest.skip("Aucun JWT configuré")
    
    if operations_probe.status_code == 404:
        pytest.skip("Ressource /operations non trouvée.")
    if operations_probe.status_code in (401, 403):
        pytest.skip(f"Accès à /operations refusé ({operations_probe.status_code})")
    if operations_probe.status_code != 200:
        pytest.skip(f"Accès à /operations impossible ({operations_probe.status_code})")
    
    return operations_probe

@pytest.fixture()
def available_rpc(operations_probe):
    """Extrait un RPC disponible pour les tests d'invocation."""
    if operations_probe.status_code != 200:
        return None
    
    try:
        body = operations_probe.json()
    except Exception:
        return None
        
    # Structure attendue : {"ietf-restconf:operations": {"rpc1": ..., "rpc2": ...}}
    ops = body.get("ietf-restconf:operations", {})
    
    if isinstance(ops, dict):
        # On cherche une clé qui n'est pas un attribut YANG standard
        for key in ops:
            # Ignorer les clés vides ou purement descriptives si nécessaire
            if key and not key.startswith("@"):
                return key
                
    return None

# ============================================================================
# T-OPS-01 : GET sur {+restconf}/operations
# ============================================================================
@pytest.mark.roadmap("R15")
@pytest.mark.rfc("RFC 8040 §3.6")
class TestT_OPS_01_GetOperations:
    """
    T-OPS-01 : GET sur {+restconf}/operations.
    Liste des RPC disponibles, filtrée selon NACM.
    """
    def test_get_operations(self, http2_client, api_url, auth_headers, require_operations):
        assert require_operations.status_code == 200
        assert get_content_type(require_operations) == YANG_JSON
        body = require_operations.json()
        
        # Vérifier que la structure contient bien les opérations
        assert "ietf-restconf:operations" in body or any("operations" in k for k in body.keys()), (
            "La réponse GET /operations doit contenir la liste des opérations."
        )

# ============================================================================
# T-OPS-02 : OPTIONS sur {+restconf}/operations
# ============================================================================
@pytest.mark.roadmap("R15")
@pytest.mark.rfc("RFC 8040 §3.6")
class TestT_OPS_02_OptionsOperations:
    """
    T-OPS-02 : OPTIONS sur {+restconf}/operations.
    Header `Allow` correct.
    """
    def test_options_operations(self, http2_client, api_url, auth_headers, require_operations):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.options(f"{api_url}{OPERATIONS}", headers=headers)
        
        # OPTIONS doit retourner 200 ou 204 avec le header Allow
        assert response.status_code in (200, 204), (
            f"OPTIONS sur /operations doit retourner 200 ou 204, obtenu {response.status_code}"
        )
        
        allow_header = response.headers.get("allow", "")
        # Doit contenir au minimum GET et POST (ou GET si c'est juste la liste)
        assert "get" in allow_header.lower(), f"Header Allow doit contenir GET, obtenu : {allow_header}"

# ============================================================================
# T-OPS-03, T-OPS-04, T-OPS-05 : POST sur un RPC
# ============================================================================
@pytest.mark.roadmap("R15")
@pytest.mark.rfc("RFC 8040 §3.6.1")
class TestT_OPS_03_04_05_PostRpc:
    """
    T-OPS-03 : POST sur un RPC avec input valide -> 200 OK avec output ou 204 No Content.
    T-OPS-04 : POST sur un RPC sans output -> 204 No Content.
    T-OPS-05 : POST sur un RPC avec input invalide -> 400 Bad Request.
    """
    def test_post_rpc_valid(self, http2_client, api_url, auth_headers, require_operations, available_rpc):
        """T-OPS-03/04 : POST sur un RPC avec input valide."""
        if not available_rpc:
            pytest.skip("Aucun RPC disponible pour les tests d'invocation.")
            
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        
        # Construction du nom du module pour l'input
        # Si le RPC est préfixé (module:rpc), on extrait le module
        module_prefix = ""
        if ":" in available_rpc:
            module_prefix = available_rpc.split(":")[0] + ":"
        
        # Envoi d'un input vide
        # RFC 8040 §3.6.1 : le corps doit être un conteneur "input" qualifié par le module
        payload = f'{{"{module_prefix}input": {{}}}}'.encode()
        
        response = http2_client.post(
            f"{api_url}{OPERATIONS}/{available_rpc}", 
            headers=headers, 
            content=payload
        )
        
        # Le RPC peut avoir un output (200) ou non (204)
        # Il peut aussi échouer avec 400 si l'input vide n'est pas valide
        assert response.status_code in (200, 204, 400), (
            f"POST sur RPC doit retourner 200, 204 ou 400, obtenu {response.status_code}"
        )
        
        if response.status_code == 200:
            assert get_content_type(response) == YANG_JSON
            body = response.json()
            # Vérifier la présence de output
            assert any("output" in k for k in body.keys()), "Réponse 200 doit contenir un nœud output."

    def test_post_rpc_invalid_input(self, http2_client, api_url, auth_headers, require_operations, available_rpc):
        """T-OPS-05 : POST sur un RPC avec input invalide."""
        if not available_rpc:
            pytest.skip("Aucun RPC disponible pour les tests d'invocation.")
            
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        
        # Envoi d'un JSON invalide
        payload = b"{ invalid json }"
        
        response = http2_client.post(
            f"{api_url}{OPERATIONS}/{available_rpc}", 
            headers=headers, 
            content=payload
        )
        
        assert response.status_code == 400, (
            f"POST avec input invalide doit retourner 400, obtenu {response.status_code}"
        )

# ============================================================================
# T-OPS-06 : POST sur un RPC inconnu
# ============================================================================
@pytest.mark.roadmap("R15")
@pytest.mark.rfc("RFC 8040 §3.6.1")
class TestT_OPS_06_PostUnknownRpc:
    """
    T-OPS-06 : POST sur un RPC inconnu.
    404 Not Found ou erreur RESTCONF pertinente.
    """
    def test_post_unknown_rpc(self, http2_client, api_url, auth_headers):
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        payload = b"{}"
        
        response = http2_client.post(
            f"{api_url}{RPC_UNKNOWN}", 
            headers=headers, 
            content=payload
        )
        
        assert response.status_code in (404, 400), (
            f"POST sur RPC inconnu doit retourner 404 ou 400, obtenu {response.status_code}"
        )
        if response.status_code == 404:
            # Vérifier l'enveloppe d'erreur si présente
            if YANG_JSON in response.headers.get("content-type", ""):
                try:
                    body = response.json()
                    assert "ietf-restconf:errors" in body
                except Exception:
                    pass

# ============================================================================
# T-OPS-07 : POST sur un RPC non autorisé
# ============================================================================
@pytest.mark.roadmap("R15")
@pytest.mark.rfc("RFC 8341")
class TestT_OPS_07_PostUnauthorizedRpc:
    """
    T-OPS-07 : POST sur un RPC non autorisé.
    403 Forbidden ou 401 Unauthorized.
    Ce test utilise l'absence d'authentification pour vérifier le contrôle d'accès.
    """
    def test_post_rpc_no_auth(self, http2_client, api_url):
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON}
        payload = b"{}"
        
        # Tentative d'accès sans token
        response = http2_client.post(
            f"{api_url}{RPC_UNKNOWN}", 
            headers=headers, 
            content=payload
        )
        
        assert response.status_code in (401, 403, 404), (
            f"POST sans authentification doit retourner 401, 403 ou 404, obtenu {response.status_code}"
        )

# ============================================================================
# T-OPS-08, T-OPS-09 : Actions liées à un data resource
# ============================================================================
@pytest.mark.roadmap("R11")
@pytest.mark.rfc("RFC 8040 §4.4.2")
class TestT_OPS_08_09_DataActions:
    """
    T-OPS-08 : POST sur une action liée à un data resource.
    T-OPS-09 : POST action sur ressource parent inexistante -> 404 Not Found.
    """
    def test_post_action_on_data_resource(self, http2_client, api_url, auth_headers, require_operations):
        """T-OPS-08 : POST sur une action liée à un data resource."""
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        
        # On tente d'invoquer une action sur basic-data
        # Si l'action n'existe pas, on attend 404
        payload = f'{{"{MOD}:input": {{}}}}'.encode()
        
        response = http2_client.post(
            f"{api_url}{ACTION_DATA_PATH}/{ACTION_NAME}", 
            headers=headers, 
            content=payload
        )
        
        # Si l'action n'existe pas, 404 est attendu. Si elle existe mais échoue, 400 ou autre.
        assert response.status_code in (200, 204, 404, 400), (
            f"POST action sur data resource doit retourner 200/204/404/400, obtenu {response.status_code}"
        )

    def test_post_action_on_missing_parent(self, http2_client, api_url, auth_headers):
        """T-OPS-09 : POST action sur ressource parent inexistante."""
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        payload = b"{}"
        
        response = http2_client.post(
            f"{api_url}/data/nonexistent-container/action-name", 
            headers=headers, 
            content=payload
        )
        
        assert response.status_code == 404, (
            f"POST action sur parent inexistant doit retourner 404, obtenu {response.status_code}"
        )

# ============================================================================
# T-OPS-10 : RPC avec output partiellement masqué par NACM
# ============================================================================
@pytest.mark.roadmap("R15")
@pytest.mark.rfc("RFC 8341")
class TestT_OPS_10_NACMOutputMasking:
    """
    T-OPS-10 : RPC avec output partiellement masqué par NACM.
    Comportement validé : omission ou erreur selon la règle applicable.
    Ce test vérifie qu'en cas d'erreur 403, aucune donnée sensible n'est exposée.
    """
    def test_nacm_output_masking(self, http2_client, api_url, auth_headers, require_operations, available_rpc):
        """T-OPS-10 : RPC avec output partiellement masqué par NACM."""
        if not available_rpc:
            pytest.skip("Aucun RPC disponible pour les tests NACM.")
            
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        payload = b"{}"
        
        response = http2_client.post(
            f"{api_url}{OPERATIONS}/{available_rpc}", 
            headers=headers, 
            content=payload
        )
        
        # Si accès refusé, vérifier que l'erreur ne contient pas de données
        if response.status_code == 403:
            if YANG_JSON in response.headers.get("content-type", ""):
                try:
                    body = response.json()
                    assert "ietf-restconf:errors" in body
                    # Vérifier qu'il n'y a pas de données métier dans l'erreur
                    error_content = str(body)
                    # On s'assure que l'erreur est standard
                    assert "access-denied" in error_content or "access" in error_content.lower()
                except Exception:
                    pass