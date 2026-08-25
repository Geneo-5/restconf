# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN

"""Tests de conformité RESTCONF - Section 9 : Media types et négociation de contenu.

Couvre les items T-MEDIA-01 à T-MEDIA-10 de la ROADMAP.md.
RFC liées : RFC 8040, RFC 7951, RFC 9110, RFC 8072.
Items liés : R41, R22, R24, R26, A19.
"""

from __future__ import annotations

import pytest

# ---------------------------------------------------------------------------
# Références normatives et chemins
# ---------------------------------------------------------------------------

YANG_JSON = "application/yang-data+json"
YANG_XML = "application/yang-data+xml"
YANG_PATCH_JSON = "application/yang-patch+json"
YANG_PATCH_XML = "application/yang-patch+xml"
SSE_MEDIA_TYPE = "text/event-stream"
MOD = "restconf-test"

BASIC_DATA = "/data/restconf-test:basic-data"
SYSTEM_CONFIG = "/data/restconf-test:system/config"
INTERFACES = "/data/restconf-test:interfaces"

# Chemin SSE (à adapter selon l'implémentation des streams)
STREAM_PATH = "/streams/stream/NETCONF"


def get_content_type(response) -> str:
    """Retourne le Content-Type de la réponse, sans ses paramètres (charset…)."""
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


# ============================================================================
# T-MEDIA-01 : Requête avec Accept: application/yang-data+json
# ============================================================================
@pytest.mark.roadmap("R41")
@pytest.mark.rfc("RFC 8040 §4.2")
@pytest.mark.rfc("RFC 7951")
class TestT_MEDIA_01_AcceptJson:
    """
    T-MEDIA-01 : Requête avec Accept: application/yang-data+json.
    Réponse JSON YANG.
    """

    def test_accept_json(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers)
        assert response.status_code == 200
        assert get_content_type(response) == YANG_JSON, (
            f"Content-Type attendu {YANG_JSON!r}, "
            f"obtenu {response.headers.get('content-type')!r}"
        )
        body = response.json()
        assert isinstance(body, dict), "Le corps JSON YANG doit être un objet."
        assert f"{MOD}:basic-data" in body


# ============================================================================
# T-MEDIA-02 : Requête avec Accept: application/yang-data+xml
# ============================================================================
@pytest.mark.roadmap("R41")
@pytest.mark.rfc("RFC 8040 §4.2")
class TestT_MEDIA_02_AcceptXml:
    """
    T-MEDIA-02 : Requête avec Accept: application/yang-data+xml.
    Réponse XML YANG.
    """

    def test_accept_xml(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_XML, **auth_headers}
        response = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers)
        assert response.status_code == 200
        assert get_content_type(response) == YANG_XML, (
            f"Content-Type attendu {YANG_XML!r}, "
            f"obtenu {response.headers.get('content-type')!r}"
        )
        # Vérification basique que le corps est du XML
        assert response.text.strip().startswith("<"), (
            "Le corps de la réponse doit être du XML."
        )


# ============================================================================
# T-MEDIA-03 : Requête avec Accept non supporté
# ============================================================================
@pytest.mark.roadmap("R41")
@pytest.mark.rfc("RFC 8040 §4.2")
@pytest.mark.rfc("RFC 9110 §12.5.6")
class TestT_MEDIA_03_AcceptNotSupported:
    """
    T-MEDIA-03 : Requête avec Accept non supporté.
    406 Not Acceptable.
    """

    def test_accept_not_supported(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": "application/unknown-media-type+xyz", **auth_headers}
        response = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers)
        assert response.status_code == 406, (
            f"GET avec Accept non supporté doit retourner 406 Not Acceptable, "
            f"obtenu {response.status_code}"
        )


# ============================================================================
# T-MEDIA-04 : Requête avec Content-Type non supporté
# ============================================================================
@pytest.mark.roadmap("R41")
@pytest.mark.rfc("RFC 8040 §4.4.1")
@pytest.mark.rfc("RFC 9110 §12.5.15")
class TestT_MEDIA_04_ContentTypeNotSupported:
    """
    T-MEDIA-04 : Requête avec Content-Type non supporté.
    415 Unsupported Media Type.
    """

    def test_content_type_not_supported(self, http2_client, api_url, auth_headers, require_rt):
        headers = {
            "Content-Type": "application/unknown-media-type+xyz",
            "Accept": YANG_JSON,
            **auth_headers,
        }
        payload = b"{}"
        response = http2_client.put(
            f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=payload
        )
        assert response.status_code == 415, (
            f"PUT avec Content-Type non supporté doit retourner 415 Unsupported Media Type, "
            f"obtenu {response.status_code}"
        )


# ============================================================================
# T-MEDIA-05 : Erreur sur requête JSON renvoyée en JSON
# ============================================================================
@pytest.mark.roadmap("R22")
@pytest.mark.rfc("RFC 8040 §7")
@pytest.mark.rfc("RFC 7951")
class TestT_MEDIA_05_ErrorInJson:
    """
    T-MEDIA-05 : Erreur sur requête JSON.
    Erreur renvoyée en application/yang-data+json si possible.
    """

    def test_error_returned_in_json(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        # Requête vers une ressource inexistante pour déclencher une erreur
        response = http2_client.get(
            f"{api_url}/data/restconf-test:nonexistent-resource", headers=headers
        )
        assert response.status_code in (404, 400), (
            f"GET sur ressource inexistante doit retourner 404 ou 400, "
            f"obtenu {response.status_code}"
        )
        # L'erreur doit être renvoyée dans le même media type que la requête
        assert get_content_type(response) == YANG_JSON, (
            f"L'erreur doit être renvoyée en {YANG_JSON!r}, "
            f"obtenu {response.headers.get('content-type')!r}"
        )
        body = response.json()
        assert "ietf-restconf:errors" in body, (
            "L'erreur doit utiliser l'enveloppe ietf-restconf:errors."
        )


# ============================================================================
# T-MEDIA-06 : Erreur sur requête XML renvoyée en XML
# ============================================================================
@pytest.mark.roadmap("R22")
@pytest.mark.rfc("RFC 8040 §7")
class TestT_MEDIA_06_ErrorInXml:
    """
    T-MEDIA-06 : Erreur sur requête XML.
    Erreur renvoyée en application/yang-data+xml si possible.
    """

    def test_error_returned_in_xml(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_XML, **auth_headers}
        # Requête vers une ressource inexistante pour déclencher une erreur
        response = http2_client.get(
            f"{api_url}/data/restconf-test:nonexistent-resource", headers=headers
        )
        assert response.status_code in (404, 400), (
            f"GET sur ressource inexistante doit retourner 404 ou 400, "
            f"obtenu {response.status_code}"
        )
        # L'erreur doit être renvoyée dans le même media type que la requête
        assert get_content_type(response) == YANG_XML, (
            f"L'erreur doit être renvoyée en {YANG_XML!r}, "
            f"obtenu {response.headers.get('content-type')!r}"
        )
        # Vérification basique que le corps contient l'enveloppe d'erreur XML
        assert "errors" in response.text.lower(), (
            "L'erreur XML doit contenir un élément 'errors'."
        )


# ============================================================================
# T-MEDIA-07 : Corps JSON mal formé
# ============================================================================
@pytest.mark.roadmap("R41")
@pytest.mark.rfc("RFC 8040 §4.4.1")
@pytest.mark.rfc("RFC 7951")
class TestT_MEDIA_07_MalformedJson:
    """
    T-MEDIA-07 : Corps JSON mal formé.
    400 Bad Request avec malformed-message ou équivalent.
    """

    def test_malformed_json(self, http2_client, api_url, auth_headers, require_rt):
        headers = {
            "Content-Type": YANG_JSON,
            "Accept": YANG_JSON,
            **auth_headers,
        }
        # JSON syntaxiquement invalide
        payload = b"{ invalid json : missing quotes }"
        response = http2_client.put(
            f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=payload
        )
        assert response.status_code == 400, (
            f"PUT avec JSON mal formé doit retourner 400 Bad Request, "
            f"obtenu {response.status_code}"
        )
        body = response.json()
        assert "ietf-restconf:errors" in body, (
            "L'erreur doit utiliser l'enveloppe ietf-restconf:errors."
        )
        # Vérifier que l'error-tag est pertinent
        errors = body.get("ietf-restconf:errors", {}).get("error", [])
        if errors:
            error_tag = errors[0].get("error-tag", "")
            assert error_tag in ("malformed-message", "invalid-value", "bad-element"), (
                f"error-tag attendu: malformed-message/invalid-value/bad-element, "
                f"obtenu: {error_tag!r}"
            )


# ============================================================================
# T-MEDIA-08 : Corps XML mal formé
# ============================================================================
@pytest.mark.roadmap("R41")
@pytest.mark.rfc("RFC 8040 §4.4.1")
class TestT_MEDIA_08_MalformedXml:
    """
    T-MEDIA-08 : Corps XML mal formé.
    400 Bad Request avec malformed-message ou équivalent.
    """

    def test_malformed_xml(self, http2_client, api_url, auth_headers, require_rt):
        headers = {
            "Content-Type": YANG_XML,
            "Accept": YANG_XML,
            **auth_headers,
        }
        # XML syntaxiquement invalide (balise non fermée)
        payload = b"<restconf-test:system-config><unclosed>"
        response = http2_client.put(
            f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=payload
        )
        assert response.status_code == 400, (
            f"PUT avec XML mal formé doit retourner 400 Bad Request, "
            f"obtenu {response.status_code}"
        )
        # L'erreur doit être en XML puisque la requête était en XML
        assert get_content_type(response) == YANG_XML, (
            f"L'erreur doit être renvoyée en {YANG_XML!r}, "
            f"obtenu {response.headers.get('content-type')!r}"
        )
        assert "errors" in response.text.lower(), (
            "L'erreur XML doit contenir un élément 'errors'."
        )


# ============================================================================
# T-MEDIA-09 : Réponse SSE
# ============================================================================
@pytest.mark.roadmap("R24")
@pytest.mark.rfc("RFC 8040 §6.2")
class TestT_MEDIA_09_SseMediaType:
    """
    T-MEDIA-09 : Réponse SSE.
    Content-Type: text/event-stream.
    Note : Ce test est conditionnel à l'implémentation des streams SSE.
    """

    def test_sse_media_type(self, http2_client, api_url, auth_headers):
        headers = {"Accept": SSE_MEDIA_TYPE, **auth_headers}
        response = http2_client.get(
            f"{api_url}{STREAM_PATH}", headers=headers
        )

        # Si les streams ne sont pas implémentés, on skip
        if response.status_code == 404:
            pytest.skip("Endpoint SSE non implémenté (404 Not Found).")
        if response.status_code in (401, 403):
            pytest.skip(f"Accès au stream refusé ({response.status_code}).")

        assert response.status_code == 200, (
            f"GET sur un stream SSE doit retourner 200 OK, "
            f"obtenu {response.status_code}"
        )
        assert get_content_type(response) == SSE_MEDIA_TYPE, (
            f"Content-Type attendu {SSE_MEDIA_TYPE!r}, "
            f"obtenu {response.headers.get('content-type')!r}"
        )


# ============================================================================
# T-MEDIA-10 : YANG Patch media type
# ============================================================================
@pytest.mark.roadmap("R26")
@pytest.mark.rfc("RFC 8072")
class TestT_MEDIA_10_YangPatchMediaType:
    """
    T-MEDIA-10 : YANG Patch.
    Content-Type application/yang-patch+json ou application/yang-patch+xml accepté.
    Note : Ce test est conditionnel au support de YANG Patch (RFC 8072).
    """

    def test_yang_patch_json_accepted(self, http2_client, api_url, auth_headers, require_rt):
        """T-MEDIA-10 : YANG Patch JSON accepté."""
        headers = {
            "Content-Type": YANG_PATCH_JSON,
            "Accept": YANG_JSON,
            **auth_headers,
        }
        # Envoi d'un YANG Patch minimaliste
        payload = b"""{
            "ietf-yang-patch:yang-patch": {
                "patch-id": "test-patch-01",
                "edit": [
                    {
                        "edit-id": "edit-1",
                        "operation": "merge",
                        "target": "/restconf-test:basic-data",
                        "value": {
                            "restconf-test:basic-data": {}
                        }
                    }
                ]
            }
        }"""
        response = http2_client.patch(
            f"{api_url}{BASIC_DATA}", headers=headers, content=payload
        )

        # Si YANG Patch n'est pas supporté, 415 ou 404 est acceptable
        if response.status_code in (415, 404, 405):
            pytest.skip(
                f"YANG Patch non supporté ou media type non reconnu "
                f"({response.status_code})."
            )

        # Si supporté, la réponse doit être un yang-patch-status
        assert response.status_code in (200, 400), (
            f"PATCH YANG Patch doit retourner 200 ou 400, "
            f"obtenu {response.status_code}"
        )
        if response.status_code == 200:
            body = response.json()
            assert "ietf-yang-patch:yang-patch-status" in body, (
                "La réponse YANG Patch doit contenir ietf-yang-patch:yang-patch-status."
            )

    def test_yang_patch_xml_accepted(self, http2_client, api_url, auth_headers, require_rt):
        """T-MEDIA-10 : YANG Patch XML accepté."""
        headers = {
            "Content-Type": YANG_PATCH_XML,
            "Accept": YANG_XML,
            **auth_headers,
        }
        # Envoi d'un YANG Patch XML minimaliste
        payload = b"""<?xml version="1.0" encoding="UTF-8"?>
<yang-patch xmlns="urn:ietf:params:xml:ns:yang:ietf-yang-patch">
    <patch-id>test-patch-02</patch-id>
    <edit>
        <edit-id>edit-1</edit-id>
        <operation>merge</operation>
        <target>/restconf-test:basic-data</target>
        <value>
            <basic-data xmlns="urn:restconf:test"/>
        </value>
    </edit>
</yang-patch>"""
        response = http2_client.patch(
            f"{api_url}{BASIC_DATA}", headers=headers, content=payload
        )

        # Si YANG Patch n'est pas supporté, 415 ou 404 est acceptable
        if response.status_code in (415, 404, 405):
            pytest.skip(
                f"YANG Patch non supporté ou media type non reconnu "
                f"({response.status_code})."
            )

        # Si supporté, la réponse doit être un yang-patch-status en XML
        assert response.status_code in (200, 400), (
            f"PATCH YANG Patch doit retourner 200 ou 400, "
            f"obtenu {response.status_code}"
        )
        if response.status_code == 200:
            assert "yang-patch-status" in response.text.lower(), (
                "La réponse YANG Patch XML doit contenir yang-patch-status."
            )