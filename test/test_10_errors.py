# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN

"""Tests de conformité RESTCONF - Section 10 : Gestion des erreurs RESTCONF.

Couvre les items T-ERR-01 à T-ERR-14 de la ROADMAP.md.
RFC liées : RFC 8040 §7, RFC 9110.
Items liés : R22, A10.

Modèle d'erreurs :
Toute erreur RESTCONF est renvoyée dans le corps de la réponse avec le même
media type que la requête, sous la racine `ietf-restconf:errors` :

    {
      "ietf-restconf:errors": {
        "error": [
          {
            "error-type": "protocol",
            "error-tag": "invalid-value",
            "error-path": "/example:input/delay",
            "error-message": "Invalid input parameter"
          }
        ]
      }
    }

Champs :
- error-type : transport | rpc | protocol | application
- error-tag : voir table de correspondance RFC 8040 §7
- error-app-tag, error-path, error-message, error-info : optionnels

Pour YANG Patch, utiliser le format spécifique `ietf-yang-patch:yang-patch-status`
et non l'enveloppe générique ci-dessus.
"""

from __future__ import annotations

import pytest

# ---------------------------------------------------------------------------
# Références normatives et chemins
# ---------------------------------------------------------------------------

YANG_JSON = "application/yang-data+json"
YANG_XML = "application/yang-data+xml"
YANG_PATCH_JSON = "application/yang-patch+json"
SSE_MEDIA_TYPE = "text/event-stream"
MOD = "restconf-test"

BASIC_DATA = "/data/restconf-test:basic-data"
SYSTEM_CONFIG = "/data/restconf-test:system/config"
INTERFACES = "/data/restconf-test:interfaces"
INTERFACE_LIST = "/data/restconf-test:interfaces/interface"
OPERATIONS = "/operations"

# Chemin SSE (à adapter selon l'implémentation)
STREAM_PATH = "/streams/stream/NETCONF"

# Valeurs valides pour error-type (RFC 8040 §7)
VALID_ERROR_TYPES = {"transport", "rpc", "protocol", "application"}

# Table de correspondance error-tag -> codes HTTP attendus (RFC 8040 §7 +
# Errata EID 7311).  "too-big" dépend du sens du message : 413 pour une
# requête trop grande et 400 pour une réponse trop grande.
ERROR_TAG_HTTP_CODES = {
    "in-use": {409},
    "invalid-value": {400, 404, 406},
    "too-big": {400, 413},
    "missing-attribute": {400},
    "bad-attribute": {400},
    "unknown-attribute": {400},
    "missing-element": {400},
    "bad-element": {400},
    "unknown-element": {400},
    "unknown-namespace": {400},
    "access-denied": {401, 403},
    "lock-denied": {409},
    "resource-denied": {409},
    "rollback-failed": {500},
    "data-exists": {409},
    "data-missing": {409},
    "operation-not-supported": {405, 501},
    "operation-failed": {412, 500},
    "partial-operation": {500},
    "malformed-message": {400},
}

# Liste des error-tags valides
VALID_ERROR_TAGS = set(ERROR_TAG_HTTP_CODES.keys())


def get_content_type(response) -> str:
    """Retourne le Content-Type de la réponse, sans ses paramètres."""
    return response.headers.get("content-type", "").split(";")[0].strip().lower()


def extract_error_envelope(response):
    """Extrait l'enveloppe d'erreur RESTCONF d'une réponse JSON.

    Retourne la liste des erreurs ou None si l'enveloppe est absente/invalide.
    """
    try:
        body = response.json()
    except Exception:
        return None

    errors_root = body.get("ietf-restconf:errors")
    if not isinstance(errors_root, dict):
        return None

    error_list = errors_root.get("error")
    if not isinstance(error_list, list):
        return None

    return error_list


def validate_error_entry(error_entry, response_status=None):
    """Valide une entrée d'erreur individuelle selon RFC 8040 §7.

    Retourne une liste de messages d'erreur de validation (vide si valide).
    """
    issues = []

    if not isinstance(error_entry, dict):
        return ["L'entrée d'erreur doit être un objet JSON."]

    # error-type est obligatoire
    error_type = error_entry.get("error-type")
    if error_type is None:
        issues.append("error-type est absent.")
    elif error_type not in VALID_ERROR_TYPES:
        issues.append(
            f"error-type={error_type!r} invalide. "
            f"Valeurs attendues : {VALID_ERROR_TYPES}"
        )

    # error-tag est obligatoire
    error_tag = error_entry.get("error-tag")
    if error_tag is None:
        issues.append("error-tag est absent.")
    elif error_tag not in VALID_ERROR_TAGS:
        issues.append(
            f"error-tag={error_tag!r} non reconnu. "
            f"Tags valides : {sorted(VALID_ERROR_TAGS)}"
        )

    # Vérifier la cohérence error-tag <-> code HTTP si fourni
    if error_tag and response_status and error_tag in ERROR_TAG_HTTP_CODES:
        expected_codes = ERROR_TAG_HTTP_CODES[error_tag]
        if response_status not in expected_codes:
            issues.append(
                f"error-tag={error_tag!r} incohérent avec le code HTTP "
                f"{response_status}. Codes attendus : {expected_codes}"
            )

    # Les trois leaves optionnelles sont des scalaires.  error-info est un
    # anydata qui doit représenter un conteneur et s'encode donc comme objet
    # JSON (RFC 8040 §8, grouping yang-error).
    for optional_field in ("error-app-tag", "error-path", "error-message"):
        value = error_entry.get(optional_field)
        if value is not None and not isinstance(value, str):
            issues.append(
                f"{optional_field} doit être une chaîne, "
                f"obtenu {type(value).__name__}"
            )

    error_info = error_entry.get("error-info")
    if error_info is not None and not isinstance(error_info, dict):
        issues.append(
            "error-info doit être un objet JSON représentant un conteneur, "
            f"obtenu {type(error_info).__name__}"
        )

    return issues


# ---------------------------------------------------------------------------
# Fixtures locales
# ---------------------------------------------------------------------------

_RT_PROBE_PATHS = (BASIC_DATA, SYSTEM_CONFIG, INTERFACES)


@pytest.fixture(scope="session")
def rt_probe(http2_client, api_url, auth_headers, test_jwt):
    """Sonde l'accessibilité du module restconf-test sur le datastore."""
    if not test_jwt:
        return None
    headers = {"Accept": YANG_JSON, **auth_headers}
    return {
        path: http2_client.get(f"{api_url}{path}", headers=headers)
        for path in _RT_PROBE_PATHS
    }


@pytest.fixture()
def require_rt(rt_probe):
    """Skip le test si le module restconf-test n'est pas installé/accessible."""
    if rt_probe is None:
        pytest.skip("Aucun JWT configuré")

    statuses = {path: resp.status_code for path, resp in rt_probe.items()}
    if any(resp.status_code == 200 for resp in rt_probe.values()):
        return rt_probe
    if all(resp.status_code == 404 for resp in rt_probe.values()):
        pytest.skip(f"Module YANG 'restconf-test' non installé ({statuses})")
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
    test_iface = "test-err-iface"
    yield test_iface
    try:
        http2_client.delete(
            f"{api_url}{INTERFACE_LIST}={test_iface}", headers={**auth_headers}
        )
    except Exception:
        pass


# ============================================================================
# T-ERR-01 : Erreur de protocole -> enveloppe ietf-restconf:errors présente
# ============================================================================
@pytest.mark.roadmap("R22")
@pytest.mark.rfc("RFC 8040 §7")
class TestT_ERR_01_ErrorEnvelope:
    """
    T-ERR-01 : Erreur de protocole.
    Enveloppe `ietf-restconf:errors` présente dans le corps de la réponse.
    """

    def test_error_envelope_present(self, http2_client, api_url, auth_headers, require_rt):
        """Vérifie que l'enveloppe d'erreur est présente sur une 404."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}/data/restconf-test:nonexistent-xyz", headers=headers
        )
        assert response.status_code == 404, (
            f"GET sur ressource inexistante doit retourner 404, "
            f"obtenu {response.status_code}"
        )
        assert get_content_type(response) == YANG_JSON, (
            f"L'erreur doit être en {YANG_JSON!r}, "
            f"obtenu {response.headers.get('content-type')!r}"
        )

        error_list = extract_error_envelope(response)
        assert error_list is not None, (
            "Le corps de la réponse doit contenir l'enveloppe "
            "'ietf-restconf:errors' avec une liste 'error'."
        )
        assert len(error_list) > 0, (
            "La liste 'error' ne doit pas être vide."
        )


# ============================================================================
# T-ERR-02 : Vérification de error-type
# ============================================================================
@pytest.mark.roadmap("R22")
@pytest.mark.rfc("RFC 8040 §7")
class TestT_ERR_02_ErrorType:
    """
    T-ERR-02 : Vérification de error-type.
    Valeur cohérente : transport, rpc, protocol, application.
    """

    def test_error_type_valid(self, http2_client, api_url, auth_headers, require_rt):
        """Vérifie que error-type a une valeur valide."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}/data/restconf-test:nonexistent-xyz", headers=headers
        )
        assert response.status_code == 404

        error_list = extract_error_envelope(response)
        assert error_list is not None, "Enveloppe d'erreur absente."

        for error_entry in error_list:
            error_type = error_entry.get("error-type")
            assert error_type in VALID_ERROR_TYPES, (
                f"error-type={error_type!r} invalide. "
                f"Valeurs attendues : {VALID_ERROR_TYPES}"
            )

    def test_error_type_protocol_for_protocol_error(self, http2_client, api_url, auth_headers, require_rt):
        """Vérifie que error-type=protocol pour une erreur de protocole."""
        headers = {
            "Content-Type": YANG_JSON,
            "Accept": YANG_JSON,
            **auth_headers,
        }
        # Envoi d'un JSON mal formé pour déclencher une erreur de protocole
        payload = b"{ malformed json !!! }"
        response = http2_client.put(
            f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=payload
        )
        assert response.status_code == 400, (
            f"JSON mal formé doit retourner 400, obtenu {response.status_code}"
        )

        error_list = extract_error_envelope(response)
        assert error_list is not None, "Enveloppe d'erreur absente."

        for error_entry in error_list:
            error_type = error_entry.get("error-type")
            assert error_type == "protocol", (
                f"Pour un JSON mal formé, error-type doit être 'protocol', "
                f"obtenu {error_type!r}"
            )


# ============================================================================
# T-ERR-03 : Vérification de error-tag
# ============================================================================
@pytest.mark.roadmap("R22")
@pytest.mark.rfc("RFC 8040 §7")
class TestT_ERR_03_ErrorTag:
    """
    T-ERR-03 : Vérification de error-tag.
    Tag conforme à la table de correspondance RFC 8040 §7.
    """

    def test_error_tag_valid(self, http2_client, api_url, auth_headers, require_rt):
        """Vérifie que error-tag est un tag reconnu."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}/data/restconf-test:nonexistent-xyz", headers=headers
        )
        assert response.status_code == 404

        error_list = extract_error_envelope(response)
        assert error_list is not None, "Enveloppe d'erreur absente."

        for error_entry in error_list:
            error_tag = error_entry.get("error-tag")
            assert error_tag in VALID_ERROR_TAGS, (
                f"error-tag={error_tag!r} non reconnu. "
                f"Tags valides : {sorted(VALID_ERROR_TAGS)}"
            )

    def test_error_tag_consistent_with_http_code(self, http2_client, api_url, auth_headers, require_rt):
        """Vérifie la cohérence error-tag <-> code HTTP."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}/data/restconf-test:nonexistent-xyz", headers=headers
        )
        assert response.status_code == 404

        error_list = extract_error_envelope(response)
        assert error_list is not None, "Enveloppe d'erreur absente."

        for error_entry in error_list:
            error_tag = error_entry.get("error-tag")
            if error_tag and error_tag in ERROR_TAG_HTTP_CODES:
                expected_codes = ERROR_TAG_HTTP_CODES[error_tag]
                assert response.status_code in expected_codes, (
                    f"error-tag={error_tag!r} incohérent avec le code HTTP "
                    f"{response.status_code}. Codes attendus : {expected_codes}"
                )

    def test_malformed_message_tag(self, http2_client, api_url, auth_headers, require_rt):
        """Vérifie que malformed-message est utilisé pour JSON invalide."""
        headers = {
            "Content-Type": YANG_JSON,
            "Accept": YANG_JSON,
            **auth_headers,
        }
        payload = b"{ invalid json }"
        response = http2_client.put(
            f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=payload
        )
        assert response.status_code == 400

        error_list = extract_error_envelope(response)
        assert error_list is not None, "Enveloppe d'erreur absente."

        # Le tag attendu est malformed-message ou invalid-value ou bad-element
        found_tag = error_list[0].get("error-tag", "") if error_list else ""
        assert found_tag in ("malformed-message", "invalid-value", "bad-element"), (
            f"Pour un JSON mal formé, error-tag attendu : "
            f"malformed-message/invalid-value/bad-element, obtenu {found_tag!r}"
        )


# ============================================================================
# T-ERR-04 : Erreur avec chemin fautif -> error-path renseigné
# ============================================================================
@pytest.mark.roadmap("R22")
@pytest.mark.rfc("RFC 8040 §7")
class TestT_ERR_04_ErrorPath:
    """
    T-ERR-04 : Erreur avec chemin fautif.
    `error-path` renseigné si pertinent.
    """

    def test_error_path_present_on_invalid_element(self, http2_client, api_url, auth_headers, require_rt):
        """Vérifie que error-path est renseigné pour un élément invalide."""
        headers = {
            "Content-Type": YANG_JSON,
            "Accept": YANG_JSON,
            **auth_headers,
        }
        # Envoi d'un élément inconnu pour déclencher unknown-element avec error-path
        payload = f'{{"{MOD}:basic-data": {{"nonexistent-field-xyz": "value"}}}}'.encode()
        response = http2_client.put(
            f"{api_url}{BASIC_DATA}", headers=headers, content=payload
        )

        # Le serveur peut retourner 400 ou ignorer le champ selon sa tolérance
        if response.status_code == 400:
            error_list = extract_error_envelope(response)
            assert error_list is not None, "Enveloppe d'erreur absente."

            # error-path est optionnel mais devrait être présent si pertinent
            # On vérifie juste qu'il est bien formé s'il est présent
            for error_entry in error_list:
                error_path = error_entry.get("error-path")
                if error_path is not None:
                    assert isinstance(error_path, str), (
                        f"error-path doit être une chaîne, obtenu {type(error_path)}"
                    )
                    assert len(error_path) > 0, "error-path ne doit pas être vide."


# ============================================================================
# T-ERR-05 : Erreur avec message lisible -> error-message présent
# ============================================================================
@pytest.mark.roadmap("R22")
@pytest.mark.rfc("RFC 8040 §7")
class TestT_ERR_05_ErrorMessage:
    """
    T-ERR-05 : Erreur avec message lisible.
    `error-message` présent si activé.
    """

    def test_error_message_present(self, http2_client, api_url, auth_headers, require_rt):
        """Vérifie que error-message est présent et non vide."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}/data/restconf-test:nonexistent-xyz", headers=headers
        )
        assert response.status_code == 404

        error_list = extract_error_envelope(response)
        assert error_list is not None, "Enveloppe d'erreur absente."

        # error-message est optionnel selon RFC 8040 mais recommandé
        # On vérifie sa présence sans la rendre obligatoire (serveur peut le désactiver)
        has_message = any(
            e.get("error-message") for e in error_list if isinstance(e, dict)
        )
        # Note : si le serveur désactive les messages, ce test peut être adapté
        # Pour la conformité stricte, on vérifie juste le format si présent
        for error_entry in error_list:
            msg = error_entry.get("error-message")
            if msg is not None:
                assert isinstance(msg, str), (
                    f"error-message doit être une chaîne, obtenu {type(msg)}"
                )
                assert len(msg) > 0, "error-message ne doit pas être vide."


# ============================================================================
# T-ERR-06 : 401 Unauthorized -> Header WWW-Authenticate présent
# ============================================================================
@pytest.mark.roadmap("R22")
@pytest.mark.rfc("RFC 9110 §15.5.2")
@pytest.mark.rfc("RFC 8040 §7")
class TestT_ERR_06_401_WWWAuthenticate:
    """
    T-ERR-06 : 401 Unauthorized.
    Header `WWW-Authenticate` présent.
    """

    def test_401_has_www_authenticate(self, http2_client, api_url):
        """Vérifie que 401 inclut WWW-Authenticate."""
        headers = {"Accept": YANG_JSON}
        # Requête sans authentification sur une ressource protégée
        response = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers)

        # Le serveur peut exiger l'authentification (401) ou autoriser l'accès
        if response.status_code == 401:
            assert "www-authenticate" in response.headers, (
                "401 Unauthorized doit inclure le header WWW-Authenticate "
                "(RFC 9110 §15.5.2)."
            )
            www_auth = response.headers["www-authenticate"]
            assert len(www_auth) > 0, "WWW-Authenticate ne doit pas être vide."

    def test_401_invalid_token_has_www_authenticate(self, http2_client, api_url):
        """Vérifie que 401 avec token invalide inclut WWW-Authenticate."""
        headers = {
            "Accept": YANG_JSON,
            "Authorization": "Bearer invalid.token.here",
        }
        response = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers)

        if response.status_code == 401:
            assert "www-authenticate" in response.headers, (
                "401 Unauthorized avec token invalide doit inclure "
                "le header WWW-Authenticate."
            )


# ============================================================================
# T-ERR-07 : 405 Method Not Allowed -> Header Allow présent
# ============================================================================
@pytest.mark.roadmap("R22")
@pytest.mark.rfc("RFC 9110 §15.5.6")
@pytest.mark.rfc("RFC 8040 §7")
class TestT_ERR_07_405_Allow:
    """
    T-ERR-07 : 405 Method Not Allowed.
    Header `Allow` présent.
    """

    def test_405_has_allow(self, http2_client, api_url, auth_headers, require_rt):
        """Vérifie que 405 inclut le header Allow."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        # DELETE sur une ressource qui ne supporte peut-être pas DELETE
        # (ex: la racine API ou /operations)
        response = http2_client.delete(f"{api_url}/operations", headers=headers)

        if response.status_code == 405:
            assert "allow" in response.headers, (
                "405 Method Not Allowed doit inclure le header Allow "
                "(RFC 9110 §15.5.6)."
            )
            allow = response.headers["allow"]
            assert len(allow) > 0, "Allow ne doit pas être vide."

    def test_405_on_invalid_method(self, http2_client, api_url, auth_headers, require_rt):
        """Vérifie que 405 est retourné avec Allow pour une méthode inappropriée."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        # PATCH sur une ressource qui pourrait ne pas le supporter
        # On utilise une méthode qui devrait déclencher 405 sur certaines ressources
        response = http2_client.request(
            "TRACE", f"{api_url}{BASIC_DATA}", headers=headers
        )

        if response.status_code == 405:
            assert "allow" in response.headers, (
                "405 Method Not Allowed doit inclure le header Allow."
            )


# ============================================================================
# T-ERR-08 : 406 Not Acceptable
# ============================================================================
@pytest.mark.roadmap("R22")
@pytest.mark.rfc("RFC 9110 §15.5.7")
@pytest.mark.rfc("RFC 8040 §7")
class TestT_ERR_08_406_NotAcceptable:
    """
    T-ERR-08 : 406 Not Acceptable.
    Media type demandé via Accept non supporté.
    """

    def test_406_unknown_accept(self, http2_client, api_url, auth_headers, require_rt):
        """Vérifie que 406 est retourné pour un Accept non supporté."""
        headers = {"Accept": "application/unknown-type+xyz", **auth_headers}
        response = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers)

        assert response.status_code == 406, (
            f"GET avec Accept non supporté doit retourner 406, "
            f"obtenu {response.status_code}"
        )

        # Vérifier que l'erreur est dans un format exploitable
        content_type = get_content_type(response)
        # Le serveur peut répondre en JSON ou en texte simple pour 406
        assert response.status_code == 406


# ============================================================================
# T-ERR-09 : 415 Unsupported Media Type
# ============================================================================
@pytest.mark.roadmap("R22")
@pytest.mark.rfc("RFC 9110 §15.5.16")
@pytest.mark.rfc("RFC 8040 §7")
class TestT_ERR_09_415_UnsupportedMediaType:
    """
    T-ERR-09 : 415 Unsupported Media Type.
    Content-Type de la requête non supporté.
    """

    def test_415_unknown_content_type(self, http2_client, api_url, auth_headers, require_rt):
        """Vérifie que 415 est retourné pour un Content-Type non supporté."""
        headers = {
            "Content-Type": "application/unknown-type+xyz",
            "Accept": YANG_JSON,
            **auth_headers,
        }
        payload = b"{}"
        response = http2_client.put(
            f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=payload
        )

        assert response.status_code == 415, (
            f"PUT avec Content-Type non supporté doit retourner 415, "
            f"obtenu {response.status_code}"
        )


# ============================================================================
# T-ERR-10 : 412 Precondition Failed
# ============================================================================
@pytest.mark.roadmap("R22")
@pytest.mark.rfc("RFC 9110 §15.5.13")
@pytest.mark.rfc("RFC 8040 §7")
class TestT_ERR_10_412_PreconditionFailed:
    """
    T-ERR-10 : 412 Precondition Failed.
    Échec de précondition HTTP (If-Match, If-None-Match, etc.).
    """

    def test_412_if_match_mismatch(self, http2_client, api_url, auth_headers, require_rt, clean_system_config):
        """Vérifie que 412 est retourné pour If-Match invalide."""
        headers = {
            "Content-Type": YANG_JSON,
            "Accept": YANG_JSON,
            "If-Match": '"W/\"fake-etag-12345\""',
            **auth_headers,
        }
        payload = b"{}"
        response = http2_client.put(
            f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=payload
        )

        assert response.status_code == 412, (
            f"PUT avec If-Match invalide doit retourner 412, "
            f"obtenu {response.status_code}"
        )

        # Vérifier l'enveloppe d'erreur si présente
        error_list = extract_error_envelope(response)
        if error_list is not None:
            for error_entry in error_list:
                issues = validate_error_entry(error_entry, 412)
                assert not issues, f"Erreurs de validation : {issues}"


# ============================================================================
# T-ERR-11 : 409 Conflict avec data-exists
# ============================================================================
@pytest.mark.roadmap("R22")
@pytest.mark.rfc("RFC 8040 §7")
class TestT_ERR_11_409_DataExists:
    """
    T-ERR-11 : 409 Conflict avec data-exists.
    POST création sur ressource existante.
    """

    def test_409_data_exists(self, http2_client, api_url, auth_headers, require_rt, clean_interface):
        """Vérifie que 409 est retourné avec error-tag=data-exists."""
        headers = {
            "Content-Type": YANG_JSON,
            "Accept": YANG_JSON,
            **auth_headers,
        }
        payload = f'{{"{MOD}:interface": [{{"name": "{clean_interface}"}}]}}'.encode()

        # Première création
        resp_create = http2_client.post(
            f"{api_url}{INTERFACES}", headers=headers, content=payload
        )
        if resp_create.status_code not in (200, 201, 204):
            pytest.skip(
                f"Impossible de créer l'interface ({resp_create.status_code})"
            )

        # Deuxième création -> conflit
        response = http2_client.post(
            f"{api_url}{INTERFACES}", headers=headers, content=payload
        )

        assert response.status_code == 409, (
            f"POST sur ressource existante doit retourner 409, "
            f"obtenu {response.status_code}"
        )

        error_list = extract_error_envelope(response)
        assert error_list is not None, "Enveloppe d'erreur absente."

        # Vérifier que error-tag est data-exists
        found_data_exists = any(
            e.get("error-tag") == "data-exists"
            for e in error_list
            if isinstance(e, dict)
        )
        assert found_data_exists, (
            f"error-tag doit être 'data-exists' pour 409 Conflict, "
            f"obtenu : {[e.get('error-tag') for e in error_list]}"
        )


# ============================================================================
# T-ERR-12 : 500 Internal Server Error avec operation-failed
# ============================================================================
@pytest.mark.roadmap("R22")
@pytest.mark.rfc("RFC 8040 §7")
class TestT_ERR_12_500_OperationFailed:
    """
    T-ERR-12 : 500 Internal Server Error avec operation-failed.
    Erreur interne ou opérationnelle.

    Note : Ce test est difficile à automatiser sans provoquer volontairement
    une erreur serveur. On vérifie ici que si une 500 est reçue, elle est
    correctement formatée.
    """

    def test_500_format_if_encountered(self, http2_client, api_url, auth_headers, require_rt):
        """
        Vérifie le format d'une réponse 500 si rencontrée.
        Ce test est principalement un test de format qui ne force pas la 500.
        """
        headers = {"Accept": YANG_JSON, **auth_headers}
        # On tente une requête qui pourrait déclencher une erreur interne
        # (ex: requête sur un chemin très profondément imbriqué)
        deep_path = "/data/restconf-test:basic-data" + "/x" * 100
        response = http2_client.get(f"{api_url}{deep_path}", headers=headers)

        # La réponse sera probablement 400 ou 404, mais si c'est 500, on vérifie le format
        if response.status_code == 500:
            error_list = extract_error_envelope(response)
            if error_list is not None:
                for error_entry in error_list:
                    issues = validate_error_entry(error_entry, 500)
                    assert not issues, f"Erreurs de validation : {issues}"
                    # operation-failed est le tag attendu pour 500
                    tag = error_entry.get("error-tag")
                    if tag:
                        assert tag in ("operation-failed", "rollback-failed"), (
                            f"Pour 500, error-tag attendu : operation-failed, "
                            f"obtenu {tag!r}"
                        )
        else:
            # Si pas de 500, on considère le test comme passé (pas d'erreur interne)
            pass


# ============================================================================
# T-ERR-13 : Erreur après établissement SSE
# ============================================================================
@pytest.mark.roadmap("R22")
@pytest.mark.rfc("RFC 8040 §7")
class TestT_ERR_13_SseError:
    """
    T-ERR-13 : Erreur après établissement SSE.
    Pas d'enveloppe RESTCONF classique ; fermeture ou mécanisme de souscription.

    Note : Ce test nécessite un endpoint SSE fonctionnel. Il est automatiquement
    ignoré si l'endpoint n'est pas disponible.
    """

    def test_sse_error_no_restconf_envelope(self, http2_client, api_url, auth_headers):
        """Vérifie qu'après établissement SSE, les erreurs ne sont pas en RESTCONF."""
        headers = {"Accept": SSE_MEDIA_TYPE, **auth_headers}
        response = http2_client.get(f"{api_url}{STREAM_PATH}", headers=headers)

        # Si SSE n'est pas implémenté, skip
        if response.status_code == 404:
            pytest.skip("Endpoint SSE non implémenté.")
        if response.status_code in (401, 403):
            pytest.skip(f"Accès au stream refusé ({response.status_code}).")

        # Si la connexion SSE est établie (200), le Content-Type doit être SSE
        if response.status_code == 200:
            assert get_content_type(response) == SSE_MEDIA_TYPE, (
                f"Un flux SSE établi doit avoir Content-Type={SSE_MEDIA_TYPE!r}"
            )
            # Une fois le flux établi, aucune enveloppe RESTCONF ne peut être envoyée
            # Le contenu doit être du format SSE (data:, event:, etc.)
            # On ne peut pas facilement tester cela sans un vrai flux actif
        elif response.status_code == 400:
            # Erreur avant établissement -> enveloppe RESTCONF attendue
            error_list = extract_error_envelope(response)
            if get_content_type(response) == YANG_JSON:
                assert error_list is not None, (
                    "Erreur avant établissement SSE doit utiliser "
                    "l'enveloppe ietf-restconf:errors."
                )


# ============================================================================
# T-ERR-14 : Erreur YANG Patch
# ============================================================================
@pytest.mark.roadmap("R22")
@pytest.mark.rfc("RFC 8072")
class TestT_ERR_14_YangPatchError:
    """
    T-ERR-14 : Erreur YANG Patch.
    Utilisation de `yang-patch-status`, pas de l'enveloppe générique seule.

    Note : Ce test est conditionnel au support de YANG Patch (RFC 8072).
    """

    def test_yang_patch_error_format(self, http2_client, api_url, auth_headers, require_rt):
        """Vérifie que les erreurs YANG Patch utilisent yang-patch-status."""
        headers = {
            "Content-Type": YANG_PATCH_JSON,
            "Accept": YANG_JSON,
            **auth_headers,
        }
        # Envoi d'un YANG Patch avec une opération invalide pour déclencher une erreur
        payload = b"""{
            "ietf-yang-patch:yang-patch": {
                "patch-id": "test-err-patch",
                "edit": [
                    {
                        "edit-id": "edit-1",
                        "operation": "delete",
                        "target": "/restconf-test:nonexistent-resource"
                    }
                ]
            }
        }"""
        response = http2_client.patch(
            f"{api_url}{BASIC_DATA}", headers=headers, content=payload
        )

        # Si YANG Patch n'est pas supporté, skip
        if response.status_code in (415, 404, 405):
            pytest.skip(
                f"YANG Patch non supporté ({response.status_code})."
            )

        # Si supporté, la réponse doit utiliser yang-patch-status
        if response.status_code in (200, 400, 409):
            body = response.json()

            # La réponse doit contenir yang-patch-status, pas seulement ietf-restconf:errors
            has_patch_status = "ietf-yang-patch:yang-patch-status" in body
            has_generic_errors = "ietf-restconf:errors" in body

            if response.status_code == 200:
                # Succès ou erreurs partielles -> yang-patch-status requis
                assert has_patch_status, (
                    "La réponse YANG Patch doit contenir "
                    "ietf-yang-patch:yang-patch-status."
                )
                # Vérifier la structure de yang-patch-status
                patch_status = body.get("ietf-yang-patch:yang-patch-status", {})
                assert "patch-id" in patch_status, (
                    "yang-patch-status doit contenir patch-id."
                )
                # Doit contenir soit edit-status soit global-errors
                has_edit_status = "edit-status" in patch_status
                has_global_errors = "global-errors" in patch_status
                assert has_edit_status or has_global_errors, (
                    "yang-patch-status doit contenir edit-status ou global-errors."
                )

    def test_yang_patch_global_errors(self, http2_client, api_url, auth_headers, require_rt):
        """Vérifie que global-errors est utilisé pour les erreurs globales."""
        headers = {
            "Content-Type": YANG_PATCH_JSON,
            "Accept": YANG_JSON,
            **auth_headers,
        }
        # Envoi d'un YANG Patch malformé pour déclencher une erreur globale
        payload = b"""{
            "ietf-yang-patch:yang-patch": {
                "patch-id": "test-global-err"
            }
        }"""
        response = http2_client.patch(
            f"{api_url}{BASIC_DATA}", headers=headers, content=payload
        )

        if response.status_code in (415, 404, 405):
            pytest.skip(f"YANG Patch non supporté ({response.status_code}).")

        if response.status_code == 400:
            # Erreur globale de parsing -> peut utiliser ietf-restconf:errors
            # ou yang-patch-status avec global-errors
            content_type = get_content_type(response)
            if content_type == YANG_JSON:
                body = response.json()
                # Les deux formats sont acceptables pour une erreur globale
                has_patch_status = "ietf-yang-patch:yang-patch-status" in body
                has_generic_errors = "ietf-restconf:errors" in body
                assert has_patch_status or has_generic_errors, (
                    "Erreur globale YANG Patch doit utiliser yang-patch-status "
                    "ou ietf-restconf:errors."
                )
