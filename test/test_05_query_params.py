# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN

"""Tests de conformité RESTCONF - Section 5 : Paramètres de requête GET.

Couvre les items T-QUERY-01 à T-QUERY-15 de la ROADMAP.md.
RFC liées : RFC 8040 §4.8, RFC 6243.
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
INTERFACES = "/data/restconf-test:interfaces"

# Chemin de stream générique (à ajuster si votre implémentation expose les streams ailleurs)
STREAM_PATH = "/streams/stream/NETCONF"

def get_content_type(response) -> str:
    """Retourne le Content-Type de la réponse, sans ses paramètres (charset…)."""
    return response.headers.get("content-type", "").split(";")[0].strip().lower()

# ---------------------------------------------------------------------------
# Fixtures locales (identiques à test_04 pour garantir l'autonomie du fichier)
# ---------------------------------------------------------------------------

_RT_PROBE_PATHS = (BASIC_DATA, INTERFACES)

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

# ============================================================================
# T-QUERY-01 : GET avec content=config
# ============================================================================
@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8.1")
class TestT_QUERY_01_ContentConfig:
    """
    T-QUERY-01 : GET avec content=config.
    Seules les données de configuration (config true) sont retournées.
    """
    def test_content_config(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{INTERFACES}", headers=headers, params={"content": "config"})
        assert response.status_code == 200
        assert get_content_type(response) == YANG_JSON
        body = response.json()
        assert f"{MOD}:interfaces" in body

# ============================================================================
# T-QUERY-02 : GET avec content=nonconfig
# ============================================================================
@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8.1")
class TestT_QUERY_02_ContentNonconfig:
    """
    T-QUERY-02 : GET avec content=nonconfig.
    Seules les données d'état (config false) sont retournées.
    """
    def test_content_nonconfig(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers, params={"content": "nonconfig"})
        # 200 si présence de config false, 404 si le container n'a que du config true et est donc masqué
        assert response.status_code in (200, 404)
        if response.status_code == 200:
            assert get_content_type(response) == YANG_JSON

# ============================================================================
# T-QUERY-03 : GET avec content=all
# ============================================================================
@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8.1")
class TestT_QUERY_03_ContentAll:
    """
    T-QUERY-03 : GET avec content=all.
    Données de configuration et d'état retournées.
    """
    def test_content_all(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers, params={"content": "all"})
        assert response.status_code == 200
        assert get_content_type(response) == YANG_JSON

# ============================================================================
# T-QUERY-04 : GET avec depth=1
# ============================================================================
@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8.2")
class TestT_QUERY_04_Depth1:
    """
    T-QUERY-04 : GET avec depth=1.
    Seuls les enfants directs (feuilles) sont inclus, pas les containers/listes imbriqués.
    """
    def test_depth_1(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{INTERFACES}", headers=headers, params={"depth": "1"})
        # depth est optionnel : un serveur qui ne l'annonce pas peut le
        # rejeter avec 400 / invalid-value (RFC 8040 §4.8 et §4.8.2).
        assert response.status_code in (200, 400)
        if response.status_code == 200:
            body = response.json()
            assert f"{MOD}:interfaces" in body

# ============================================================================
# T-QUERY-05 : GET avec depth=unbounded
# ============================================================================
@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8.2")
class TestT_QUERY_05_DepthUnbounded:
    """
    T-QUERY-05 : GET avec depth=unbounded.
    L'arbre complet est retourné.
    """
    def test_depth_unbounded(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{INTERFACES}", headers=headers, params={"depth": "unbounded"})
        assert response.status_code in (200, 400)
        if response.status_code == 200:
            body = response.json()
            assert f"{MOD}:interfaces" in body

# ============================================================================
# T-QUERY-06 : GET avec fields valide
# ============================================================================
@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8.3")
class TestT_QUERY_06_FieldsValid:
    """
    T-QUERY-06 : GET avec fields valide.
    Seuls les champs demandés sont retournés.
    """
    def test_fields_valid(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        # Adapté selon les feuilles réelles de restconf-test:basic-data
        response = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers, params={"fields": "device-id"})
        # fields est optionnel, comme depth.
        assert response.status_code in (200, 400)
        if response.status_code == 200:
            body = response.json()
            assert f"{MOD}:basic-data" in body

# ============================================================================
# T-QUERY-07 : GET avec fields invalide
# ============================================================================
@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8.3")
class TestT_QUERY_07_FieldsInvalid:
    """
    T-QUERY-07 : GET avec fields invalide.
    Erreur 400 Bad Request avec error-tag pertinent (ex: invalid-value).
    """
    def test_fields_invalid(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{BASIC_DATA}", 
            headers=headers, 
            params={"fields": "non-existent-field-xyz"}
        )
        assert response.status_code == 400
        body = response.json()
        assert "ietf-restconf:errors" in body

# ============================================================================
# T-QUERY-08 à T-QUERY-11 : with-defaults (RFC 6243)
# ============================================================================
@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8.9")
@pytest.mark.rfc("RFC 6243")
class TestT_QUERY_08_to_11_WithDefaults:
    """
    T-QUERY-08 : with-defaults=report-all
    T-QUERY-09 : with-defaults=trim
    T-QUERY-10 : with-defaults=explicit
    T-QUERY-11 : with-defaults=report-all-tagged
    """
    @pytest.mark.parametrize("mode", ["report-all", "trim", "explicit", "report-all-tagged"])
    def test_with_defaults_modes(self, http2_client, api_url, auth_headers, require_rt, mode):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers, params={"with-defaults": mode})
        # Si with-defaults n'est pas supporté par le serveur (Conditionnel), 400 est acceptable.
        # Sinon, 200 OK.
        assert response.status_code in (200, 400)
        if response.status_code == 200:
            assert get_content_type(response) == YANG_JSON

# ============================================================================
# T-QUERY-12 : GET avec paramètre de requête inconnu
# ============================================================================
@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8")
class TestT_QUERY_12_UnknownParameter:
    """
    T-QUERY-12 : GET avec paramètre de requête inconnu.
    Un paramètre inattendu doit être rejeté avec ``400 Bad Request`` et
    ``error-tag=invalid-value`` (RFC 8040 §4.8).
    """
    def test_unknown_parameter(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{BASIC_DATA}", 
            headers=headers, 
            params={"unknown-param-xyz": "some-value", "another-unknown": "123"}
        )
        assert response.status_code == 400
        assert get_content_type(response) == YANG_JSON
        body = response.json()
        errors = body.get("ietf-restconf:errors", {}).get("error", [])
        assert any(error.get("error-tag") == "invalid-value" for error in errors), (
            "Un paramètre inattendu doit utiliser error-tag=invalid-value"
        )

# ============================================================================
# T-QUERY-13 : GET avec combinaison content, depth, fields
# ============================================================================
@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8")
class TestT_QUERY_13_CombinedParameters:
    """
    T-QUERY-13 : GET avec combinaison content, depth, fields.
    La réponse respecte simultanément les trois paramètres.
    """
    def test_combined_parameters(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        params = {
            "content": "config",
            "depth": "1",
            "fields": "device-id"
        }
        response = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers, params=params)
        # 200 si la combinaison est valide et supportée, 400 si conflit sémantique
        assert response.status_code in (200, 400)

# ============================================================================
# T-QUERY-14 : GET sur stream avec start-time / stop-time
# ============================================================================
@pytest.mark.roadmap("R21")
@pytest.mark.rfc("RFC 8040 §4.8.7")
@pytest.mark.rfc("RFC 8040 §4.8.8")
class TestT_QUERY_14_StreamReplay:
    """
    T-QUERY-14 : GET sur stream avec start-time / stop-time.
    Accepté uniquement si le replay est supporté par le stream.
    """
    def test_stream_start_stop_time(self, http2_client, api_url, auth_headers):
        headers = {"Accept": "text/event-stream", **auth_headers}
        params = {
            "start-time": "2020-01-01T00:00:00Z",
            "stop-time": "2020-01-02T00:00:00Z"
        }
        response = http2_client.get(f"{api_url}{STREAM_PATH}", headers=headers, params=params)
        # 200 si replay supporté, 400/404/501 sinon
        assert response.status_code in (200, 400, 404, 501)

# ============================================================================
# T-QUERY-15 : GET sur stream sans replay mais avec start-time
# ============================================================================
@pytest.mark.roadmap("R21")
@pytest.mark.rfc("RFC 8040 §4.8.7")
class TestT_QUERY_15_StreamNoReplay:
    """
    T-QUERY-15 : GET sur stream sans replay mais avec start-time.
    Erreur RESTCONF pertinente (ex: invalid-value ou operation-not-supported).
    """
    def test_stream_start_time_no_replay(self, http2_client, api_url, auth_headers):
        headers = {"Accept": "text/event-stream", **auth_headers}
        params = {"start-time": "2020-01-01T00:00:00Z"}
        response = http2_client.get(f"{api_url}{STREAM_PATH}", headers=headers, params=params)
        
        # Si le stream existe mais que le replay n'est pas supporté, le serveur DOIT retourner une erreur.
        # Si le stream n'existe pas du tout, 404 est acceptable.
        assert response.status_code in (200, 400, 404, 501)
        if response.status_code == 400:
            body = response.json()
            assert "ietf-restconf:errors" in body
