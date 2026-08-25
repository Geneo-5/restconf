# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN

"""Tests de conformité RESTCONF - Section 13 : NMDA.

Couvre les items T-NMDA-01 à T-NMDA-11 de la ROADMAP.md.
RFC liées : RFC 8527, RFC 7952, RFC 8342.
Items liés : R5, R27, R28, R46.

Les tests sont automatiquement ignorés si NMDA n'est pas supporté ou si les
datastores testés ne sont pas exposés par le serveur.

Variables d'environnement optionnelles :
- RESTCONF_NMDA_DATA_COMPARE_PATH
- RESTCONF_NMDA_DS_COMPARE_RESOURCE
- RESTCONF_NMDA_WRITE_DATASTORE
- RESTCONF_NMDA_WRITE_RESOURCE
- RESTCONF_TEST_JWT_RESTRICTED
"""

from __future__ import annotations

import os

import pytest

# ---------------------------------------------------------------------------
# Références normatives et chemins
# ---------------------------------------------------------------------------

YANG_JSON = "application/yang-data+json"
MOD = "restconf-test"

DATA_ROOT = "/data"
DATA_BASIC_RESOURCE = "/data/restconf-test:basic-data"

# Ressource relative utilisée sous /ds/<datastore>
RESOURCE_UNDER_DS = "/restconf-test:basic-data"
WRITE_RESOURCE_UNDER_DS = "/restconf-test:system/config"

ALL_DATASTORES = {
    "operational": "/ds/ietf-datastores:operational",
    "candidate": "/ds/ietf-datastores:candidate",
    "startup": "/ds/ietf-datastores:startup",
    "intended": "/ds/ietf-datastores:intended",
    "running": "/ds/ietf-datastores:running",
}

MAIN_DATASTORES = (
    "operational",
    "candidate",
    "startup",
    "intended",
)

UNSUPPORTED_DATASTORE_PATH = "/ds/ietf-datastores:unsupported-datastore"


def get_content_type(response) -> str:
    """Retourne le Content-Type de la réponse, sans ses paramètres."""
    return response.headers.get("content-type", "").split(";")[0].strip().lower()


def assert_json_object(response) -> dict:
    """Vérifie que la réponse est un objet JSON YANG."""
    try:
        body = response.json()
    except Exception:
        pytest.fail("La réponse doit être du JSON valide.")

    assert isinstance(body, dict), (
        "La réponse JSON YANG doit être un objet."
    )

    return body


# ---------------------------------------------------------------------------
# Helpers annotations RFC 7952
# ---------------------------------------------------------------------------

def find_json_annotations(obj) -> list[tuple[str, object]]:
    """Recherche récursive des annotations JSON RFC 7952 (clés commençant par @)."""
    annotations = []

    def _walk(node) -> None:
        if isinstance(node, dict):
            for key, value in node.items():
                if key.startswith("@"):
                    annotations.append((key, value))
                _walk(value)
        elif isinstance(node, list):
            for item in node:
                _walk(item)

    _walk(obj)
    return annotations


def has_origin_annotation(annotations: list[tuple[str, object]]) -> bool:
    """Vérifie si une annotation d'origine est présente."""
    for key, _ in annotations:
        local = key[1:].split(":")[-1].lower()
        if local == "origin" or "origin" in local:
            return True
    return False


def require_with_origin_response(http2_client, api_url, auth_headers, require_nmda):
    """Récupère une réponse with-origin sur le datastore opérationnel."""
    if "operational" not in require_nmda:
        pytest.skip("Le datastore opérationnel NMDA n'est pas supporté.")

    url = f"{api_url}{ALL_DATASTORES['operational']}?with-origin"
    headers = {"Accept": YANG_JSON, **auth_headers}

    response = http2_client.get(url, headers=headers)

    if response.status_code == 400:
        pytest.skip("Le paramètre with-origin est rejeté (non supporté).")
    if response.status_code == 404:
        pytest.skip("Le datastore opérationnel ou with-origin n'est pas disponible.")
    if response.status_code != 200:
        pytest.fail(
            f"GET avec with-origin doit retourner 200, obtenu {response.status_code}"
        )

    return response


# ---------------------------------------------------------------------------
# Fixtures NMDA
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def nmda_probe(http2_client, api_url, auth_headers, test_jwt):
    """Sonde les datastores NMDA."""
    if not test_jwt:
        return None

    headers = {"Accept": YANG_JSON, **auth_headers}
    responses = {}

    for name, path in ALL_DATASTORES.items():
        try:
            responses[name] = http2_client.get(
                f"{api_url}{path}",
                headers=headers,
            )
        except Exception:
            continue

    return responses


@pytest.fixture(scope="session")
def require_nmda(nmda_probe):
    """Skip si NMDA n'est pas disponible."""
    if nmda_probe is None:
        pytest.skip("Aucun JWT configuré pour accéder aux datastores NMDA.")

    if not nmda_probe:
        pytest.skip("Aucune réponse obtenue pour les datastores NMDA.")

    supported = {
        name: response
        for name, response in nmda_probe.items()
        if response.status_code == 200
    }

    if supported:
        return supported

    statuses = {
        name: response.status_code
        for name, response in nmda_probe.items()
    }

    if all(code == 404 for code in statuses.values()):
        pytest.skip(f"NMDA non supporté ({statuses}).")

    if all(code in (401, 403) for code in statuses.values()):
        pytest.skip(f"Accès aux datastores NMDA refusé ({statuses}).")

    pytest.skip(f"Datastores NMDA inaccessibles ({statuses}).")


@pytest.fixture()
def restricted_jwt() -> str:
    """JWT restreint pour les tests NACM."""
    token = (
        os.getenv("RESTCONF_TEST_JWT_RESTRICTED")
        or os.getenv("RESTCONF_NACM_RESTRICTED_JWT")
    )
    if not token:
        pytest.skip(
            "Aucun JWT restreint configuré. Définissez "
            "RESTCONF_TEST_JWT_RESTRICTED."
        )
    return token


@pytest.fixture()
def restricted_headers(restricted_jwt: str) -> dict[str, str]:
    """Headers HTTP pour le token restreint."""
    return {
        "Authorization": f"Bearer {restricted_jwt}",
        "Accept": YANG_JSON,
    }


# ============================================================================
# T-NMDA-01 : GET sur {+restconf}/ds/operational
# ============================================================================
@pytest.mark.roadmap("R27")
@pytest.mark.rfc("RFC 8527")
@pytest.mark.rfc("RFC 8342")
class TestT_NMDA_01_GetOperational:
    """
    T-NMDA-01 : GET sur {+restconf}/ds/operational.
    Retourne les données opérationnelles si supporté.
    """

    def test_get_operational(self, nmda_probe, require_nmda):
        response = nmda_probe.get("operational")
        if response is None or response.status_code != 200:
            pytest.skip("Le datastore operational n'est pas supporté.")

        assert response.status_code == 200
        assert get_content_type(response) == YANG_JSON
        assert_json_object(response)


# ============================================================================
# T-NMDA-02 : GET sur {+restconf}/ds/candidate
# ============================================================================
@pytest.mark.roadmap("R27")
@pytest.mark.rfc("RFC 8527")
@pytest.mark.rfc("RFC 8342")
class TestT_NMDA_02_GetCandidate:
    """
    T-NMDA-02 : GET sur {+restconf}/ds/candidate.
    Retourne les données candidate si supporté.
    """

    def test_get_candidate(self, nmda_probe, require_nmda):
        response = nmda_probe.get("candidate")
        if response is None or response.status_code != 200:
            pytest.skip("Le datastore candidate n'est pas supporté.")

        assert response.status_code == 200
        assert get_content_type(response) == YANG_JSON
        assert_json_object(response)


# ============================================================================
# T-NMDA-03 : GET sur {+restconf}/ds/startup
# ============================================================================
@pytest.mark.roadmap("R27")
@pytest.mark.rfc("RFC 8527")
@pytest.mark.rfc("RFC 8342")
class TestT_NMDA_03_GetStartup:
    """
    T-NMDA-03 : GET sur {+restconf}/ds/startup.
    Retourne les données startup si supporté.
    """

    def test_get_startup(self, nmda_probe, require_nmda):
        response = nmda_probe.get("startup")
        if response is None or response.status_code != 200:
            pytest.skip("Le datastore startup n'est pas supporté.")

        assert response.status_code == 200
        assert get_content_type(response) == YANG_JSON
        assert_json_object(response)


# ============================================================================
# T-NMDA-04 : GET sur {+restconf}/ds/intended
# ============================================================================
@pytest.mark.roadmap("R27")
@pytest.mark.rfc("RFC 8527")
@pytest.mark.rfc("RFC 8342")
class TestT_NMDA_04_GetIntended:
    """
    T-NMDA-04 : GET sur {+restconf}/ds/intended.
    Retourne les données intended si supporté.
    """

    def test_get_intended(self, nmda_probe, require_nmda):
        response = nmda_probe.get("intended")
        if response is None or response.status_code != 200:
            pytest.skip("Le datastore intended n'est pas supporté.")

        assert response.status_code == 200
        assert get_content_type(response) == YANG_JSON
        assert_json_object(response)


# ============================================================================
# T-NMDA-05 : GET sur datastore non supporté
# ============================================================================
@pytest.mark.roadmap("R27")
@pytest.mark.rfc("RFC 8527")
class TestT_NMDA_05_UnsupportedDatastore:
    """
    T-NMDA-05 : GET sur datastore non supporté.
    Erreur RESTCONF pertinente.
    """

    def test_unsupported_datastore(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
    ):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{UNSUPPORTED_DATASTORE_PATH}",
            headers=headers,
        )

        if response.status_code == 401:
            pytest.skip("Le JWT fourni est refusé.")

        if response.status_code == 200:
            pytest.fail(
                "Un datastore non supporté ne doit pas retourner 200 OK."
            )

        assert response.status_code in (400, 404), (
            "Un datastore non supporté doit retourner 400 Bad Request ou "
            f"404 Not Found, obtenu {response.status_code}"
        )

        # Si le serveur renvoie 400 avec un corps JSON, l'enveloppe d'erreur
        # RESTCONF est attendue.
        if response.status_code == 400:
            if get_content_type(response) == YANG_JSON and response.content:
                try:
                    body = response.json()
                    assert "ietf-restconf:errors" in body, (
                        "Une erreur RESTCONF JSON doit utiliser "
                        "ietf-restconf:errors."
                    )
                except Exception:
                    pass


# ============================================================================
# T-NMDA-06 : Comparaison /data et /ds/<datastore>
# ============================================================================
@pytest.mark.roadmap("R5")
@pytest.mark.roadmap("R46")
@pytest.mark.rfc("RFC 8527")
class TestT_NMDA_06_DataMapping:
    """
    T-NMDA-06 : Comparaison /data et /ds/<datastore>.
    Le comportement de /data est conforme à la documentation et à RFC 8527.

    Par défaut, on compare une ressource de configuration présente dans
    /data et dans /ds/ietf-datastores:running.
    """

    def test_data_vs_running(self, http2_client, api_url, auth_headers, require_nmda):
        if "running" not in require_nmda:
            pytest.skip("Le datastore running NMDA n'est pas exposé.")

        data_path = os.getenv(
            "RESTCONF_NMDA_DATA_COMPARE_PATH",
            DATA_BASIC_RESOURCE,
        )
        ds_resource = os.getenv(
            "RESTCONF_NMDA_DS_COMPARE_RESOURCE",
            RESOURCE_UNDER_DS,
        )

        headers = {"Accept": YANG_JSON, **auth_headers}

        data_response = http2_client.get(
            f"{api_url}{data_path}",
            headers=headers,
        )
        ds_response = http2_client.get(
            f"{api_url}{ALL_DATASTORES['running']}{ds_resource}",
            headers=headers,
        )

        if data_response.status_code == 404 and ds_response.status_code == 404:
            return

        if data_response.status_code != 200 or ds_response.status_code != 200:
            pytest.skip(
                "Impossible de comparer /data et /ds/running : "
                f"/data={data_response.status_code}, "
                f"/ds/running={ds_response.status_code}"
            )

        data_body = assert_json_object(data_response)
        ds_body = assert_json_object(ds_response)

        assert data_body == ds_body, (
            "Le contenu de /data diffère de /ds/ietf-datastores:running pour "
            "la ressource de comparaison. Le mapping doit être documenté et "
            "conforme à RFC 8527."
        )


# ============================================================================
# T-NMDA-07 : Écriture via /ds/<datastore> si supporté
# ============================================================================
@pytest.mark.roadmap("R27")
@pytest.mark.rfc("RFC 8527")
class TestT_NMDA_07_WriteDatastore:
    """
    T-NMDA-07 : Écriture via /ds/<datastore> si supporté.
    Le datastore cible est correctement modifié.
    """

    def test_write_datastore(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_nmda,
    ):
        datastore_name = os.getenv("RESTCONF_NMDA_WRITE_DATASTORE", "candidate")
        write_resource = os.getenv(
            "RESTCONF_NMDA_WRITE_RESOURCE",
            WRITE_RESOURCE_UNDER_DS,
        )

        if datastore_name not in require_nmda:
            pytest.skip(
                f"Le datastore {datastore_name!r} n'est pas supporté."
            )

        datastore_path = ALL_DATASTORES[datastore_name]
        target_url = f"{api_url}{datastore_path}{write_resource}"

        headers = {
            "Content-Type": YANG_JSON,
            "Accept": YANG_JSON,
            **auth_headers,
        }

        # Nettoyage préalable pour éviter un conflit résiduel.
        try:
            http2_client.delete(target_url, headers=headers)
        except Exception:
            pass

        response = http2_client.put(
            target_url,
            headers=headers,
            content=b"{}",
        )

        if response.status_code == 401:
            pytest.skip("Le JWT fourni est refusé.")

        if response.status_code in (403, 405, 400, 404, 409):
            pytest.skip(
                f"Le datastore {datastore_name!r} n'est pas utilisable en "
                f"écriture dans cet environnement ({response.status_code})."
            )

        assert response.status_code in (200, 201, 204), (
            f"PUT sur {target_url} doit retourner 200, 201 ou 204, "
            f"obtenu {response.status_code}"
        )

        get_response = None
        try:
            get_response = http2_client.get(
                target_url,
                headers={"Accept": YANG_JSON, **auth_headers},
            )
        finally:
            try:
                http2_client.delete(target_url, headers=headers)
            except Exception:
                pass

        assert get_response is not None
        assert get_response.status_code == 200, (
            "Après écriture dans le datastore NMDA, la ressource doit être "
            f"lisible, obtenu {get_response.status_code}"
        )


# ============================================================================
# T-NMDA-08 : GET avec with-origin
# ============================================================================
@pytest.mark.roadmap("R28")
@pytest.mark.rfc("RFC 8527")
class TestT_NMDA_08_WithOrigin:
    """
    T-NMDA-08 : GET avec with-origin.
    Annotations d'origine présentes si supporté.
    """

    def test_with_origin(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_nmda,
    ):
        response = require_with_origin_response(
            http2_client,
            api_url,
            auth_headers,
            require_nmda,
        )

        body = assert_json_object(response)
        annotations = find_json_annotations(body)

        if not annotations:
            pytest.skip(
                "with-origin est accepté mais aucune annotation n'est visible ; "
                "le support effectif n'est pas démontrable."
            )

        assert has_origin_annotation(annotations), (
            "Une annotation d'origine est attendue lorsque with-origin est "
            "supporté."
        )


# ============================================================================
# T-NMDA-09 : with-origin en JSON
# ============================================================================
@pytest.mark.roadmap("R28")
@pytest.mark.rfc("RFC 8527")
@pytest.mark.rfc("RFC 7952")
class TestT_NMDA_09_WithOriginJsonAnnotations:
    """
    T-NMDA-09 : with-origin en JSON.
    Annotations JSON conformes RFC 7952.
    """

    def test_with_origin_json_annotations(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_nmda,
    ):
        response = require_with_origin_response(
            http2_client,
            api_url,
            auth_headers,
            require_nmda,
        )

        body = assert_json_object(response)
        annotations = find_json_annotations(body)

        if not annotations:
            pytest.skip(
                "with-origin est accepté mais aucune annotation JSON n'est "
                "visible."
            )

        for key, value in annotations:
            assert key.startswith("@"), (
                f"Annotation RFC 7952 invalide : {key!r} doit commencer par '@'."
            )
            assert len(key) > 1, (
                f"Annotation RFC 7952 invalide : {key!r} est vide après '@'."
            )

            local_name = key[1:].split(":")[-1].lower()
            if local_name == "origin":
                assert value, (
                    f"L'annotation d'origine {key!r} ne doit pas être vide."
                )


# ============================================================================
# T-NMDA-10 : NACM sur datastores
# ============================================================================
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8341")
@pytest.mark.rfc("RFC 8527")
class TestT_NMDA_10_NacmOnDatastores:
    """
    T-NMDA-10 : NACM sur datastores.
    Les règles NACM s'appliquent aussi via /ds/<datastore>.
    """

    def test_nacm_on_datastores(
        self,
        http2_client,
        api_url,
        require_nmda,
        restricted_headers,
    ):
        # Priorité au datastore opérationnel, sinon le premier supporté.
        if "operational" in require_nmda:
            datastore_name = "operational"
        else:
            datastore_name = next(iter(require_nmda))

        admin_response = require_nmda[datastore_name]
        if admin_response.status_code != 200:
            pytest.skip("Le datastore de référence n'est pas accessible par admin.")

        url = f"{api_url}{ALL_DATASTORES[datastore_name]}"
        restricted_response = http2_client.get(url, headers=restricted_headers)

        if restricted_response.status_code == 401:
            pytest.skip("Le JWT restreint est refusé comme invalide.")

        if restricted_response.status_code in (403, 404):
            return

        if restricted_response.status_code != 200:
            pytest.fail(
                "Réponse inattendue du token restreint sur /ds : "
                f"{restricted_response.status_code}"
            )

        try:
            admin_body = admin_response.json()
            restricted_body = restricted_response.json()
        except Exception:
            pytest.skip("Impossible de comparer les réponses JSON.")

        if not isinstance(admin_body, dict) or not isinstance(restricted_body, dict):
            pytest.skip("Les réponses ne sont pas des objets JSON comparables.")

        admin_keys = set(admin_body.keys())
        restricted_keys = set(restricted_body.keys())

        if restricted_keys < admin_keys:
            return

        pytest.skip(
            "Le token restreint voit autant de données que le token admin ; "
            "aucune règle NACM de filtrage NMDA n'est démontrable."
        )


# ============================================================================
# T-NMDA-11 : Conditional requests sur datastores
# ============================================================================
@pytest.mark.roadmap("R9")
@pytest.mark.roadmap("R42")
@pytest.mark.rfc("RFC 8527")
@pytest.mark.rfc("RFC 9110")
class TestT_NMDA_11_ConditionalRequests:
    """
    T-NMDA-11 : Conditional requests sur datastores.
    ETag/Last-Modified cohérents par datastore si implémenté.
    """

    def test_validators_consistency(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_nmda,
    ):
        headers = {"Accept": YANG_JSON, **auth_headers}
        found_validator = False

        for datastore_name, first_response in require_nmda.items():
            etag = first_response.headers.get("etag")
            last_modified = first_response.headers.get("last-modified")

            if not etag and not last_modified:
                continue

            found_validator = True

            url = f"{api_url}{ALL_DATASTORES[datastore_name]}"
            second_response = http2_client.get(url, headers=headers)

            if second_response.status_code != 200:
                continue

            etag_2 = second_response.headers.get("etag")
            last_modified_2 = second_response.headers.get("last-modified")

            if etag and etag_2:
                assert etag == etag_2, (
                    f"ETag incohérent pour {datastore_name} entre deux lectures "
                    f"consécutives : {etag!r} != {etag_2!r}"
                )

            if last_modified and last_modified_2:
                assert last_modified == last_modified_2, (
                    f"Last-Modified incohérent pour {datastore_name} entre deux "
                    f"lectures consécutives : {last_modified!r} != "
                    f"{last_modified_2!r}"
                )

        if not found_validator:
            pytest.skip(
                "Aucun validateur conditionnel (ETag/Last-Modified) n'est "
                "exposé sur les datastores NMDA."
            )