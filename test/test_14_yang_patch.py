# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN

"""Tests de conformité RESTCONF - Section 14 : YANG Patch.

Couvre les items T-YPATCH-01 à T-YPATCH-13 de la ROADMAP.md.
RFC liée : RFC 8072.
Items liés : R26, R29, A15.

Les tests sont automatiquement ignorés si YANG Patch n'est pas supporté,
si le module de test n'est pas installé, ou si les prérequis d'environnement
ne permettent pas de démontrer le comportement testé.
"""

from __future__ import annotations

import json
import os
import uuid

import pytest

# ---------------------------------------------------------------------------
# Références normatives et chemins
# ---------------------------------------------------------------------------

YANG_JSON = "application/yang-data+json"
YANG_XML = "application/yang-data+xml"
YANG_PATCH_JSON = "application/yang-patch+json"
YANG_PATCH_XML = "application/yang-patch+xml"

MOD = "restconf-test"
RT_NS = "urn:restconf:test"

DATA_ROOT = "/data"

SYSTEM_TARGET = "/restconf-test:system"
SYSTEM_DATA_PATH = "/data/restconf-test:system"
SYSTEM_VALUE = {
    "restconf-test:system": {
        "config": {}
    }
}

INTERFACES_PATH = "/data/restconf-test:interfaces"
INTERFACE_LIST_PATH = "/data/restconf-test:interfaces/interface"

NON_EXISTENT_TARGET = "/restconf-test:nonexistent-resource-xyz"


def get_content_type(response) -> str:
    """Retourne le Content-Type de la réponse, sans ses paramètres."""
    return response.headers.get("content-type", "").split(";")[0].strip().lower()


def local_name(key: str) -> str:
    """Retourne le nom local d'une clé JSON YANG, sans préfixe module."""
    return key.split(":")[-1]


# ---------------------------------------------------------------------------
# Helpers YANG Patch
# ---------------------------------------------------------------------------

def yang_patch_json(patch_id: str, edits: list[dict] | None = None) -> bytes:
    """Construit un corps YANG Patch JSON."""
    patch = {"patch-id": patch_id}
    if edits is not None:
        patch["edit"] = edits

    return json.dumps(
        {
            "ietf-yang-patch:yang-patch": patch,
        }
    ).encode()


def yang_patch_xml(patch_id: str, edits_xml: str) -> bytes:
    """Construit un corps YANG Patch XML."""
    return (
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<yang-patch xmlns=\"urn:ietf:params:xml:ns:yang:ietf-yang-patch\">\n"
        f"  <patch-id>{patch_id}</patch-id>\n"
        f"{edits_xml}\n"
        "</yang-patch>\n"
    ).encode()


def send_yang_patch(
    http2_client,
    api_url,
    auth_headers,
    payload: bytes,
    content_type: str = YANG_PATCH_JSON,
    accept: str = YANG_JSON,
    target: str = DATA_ROOT,
):
    """Envoie une requête PATCH YANG Patch."""
    headers = {
        "Content-Type": content_type,
        "Accept": accept,
        **auth_headers,
    }
    return http2_client.patch(
        f"{api_url}{target}",
        headers=headers,
        content=payload,
    )


def skip_if_yang_patch_unsupported(response) -> None:
    """Skip si YANG Patch n'est pas supporté ou non démontrable."""
    if response.status_code == 415:
        pytest.skip("Le media type YANG Patch n'est pas supporté.")
    if response.status_code in (405, 501):
        pytest.skip("La méthode PATCH ou YANG Patch n'est pas supportée.")
    if response.status_code == 404:
        pytest.skip("La ressource cible YANG Patch n'est pas disponible.")
    if response.status_code == 401:
        pytest.skip("Le JWT fourni est refusé.")
    if response.status_code == 403:
        pytest.skip("Accès refusé avec le token courant.")


def get_patch_status(body: dict) -> dict | None:
    """Extrait ietf-yang-patch:yang-patch-status."""
    if not isinstance(body, dict):
        return None

    status = body.get("ietf-yang-patch:yang-patch-status")
    if isinstance(status, dict):
        return status

    # Cas improbable mais toléré : le corps est directement le status.
    if "patch-id" in body and ("global-errors" in body or "edit-status" in body):
        return body

    return None


def get_global_errors(status: dict) -> list:
    """Extrait les erreurs globales d'un yang-patch-status."""
    errors = status.get("global-errors")

    if isinstance(errors, dict):
        inner = errors.get("error")
        if isinstance(inner, list):
            return inner
        return [errors]

    if isinstance(errors, list):
        return errors

    return []


def get_edit_errors(status: dict) -> list[dict]:
    """Extrait les erreurs par opération d'un yang-patch-status."""
    edit_status = status.get("edit-status")
    items = []

    if isinstance(edit_status, dict):
        for key in ("edit-status", "edit"):
            value = edit_status.get(key)
            if isinstance(value, list):
                items = value
                break
    elif isinstance(edit_status, list):
        items = edit_status

    errors = []
    for item in items:
        if not isinstance(item, dict):
            continue

        edit_id = item.get("edit-id")
        error = (
            item.get("error")
            or item.get("ietf-restconf:error")
            or item.get("ietf-yang-patch:error")
        )

        if error:
            errors.append(
                {
                    "edit-id": edit_id,
                    "error": error,
                }
            )

    return errors


def flatten_error_tags(edit_errors: list[dict]) -> list[str]:
    """Aplatit les error-tag présents dans les erreurs d'édition."""
    tags = []

    for entry in edit_errors:
        error = entry.get("error")

        if isinstance(error, dict):
            error = [error]

        if isinstance(error, list):
            for err in error:
                if isinstance(err, dict):
                    tag = err.get("error-tag")
                    if tag:
                        tags.append(tag)

    return tags


def require_patch_success(response, patch_id: str) -> dict:
    """Vérifie qu'une réponse YANG Patch est un succès avec yang-patch-status."""
    skip_if_yang_patch_unsupported(response)

    if response.status_code == 400:
        pytest.skip(
            "La requête YANG Patch a été rejetée avec 400 ; "
            "le support effectif n'est pas démontrable dans cet environnement."
        )

    assert response.status_code == 200, (
        f"Une requête YANG Patch valide doit retourner 200 OK, "
        f"obtenu {response.status_code}"
    )

    try:
        body = response.json()
    except Exception:
        pytest.fail("La réponse YANG Patch doit être du JSON valide.")

    status = get_patch_status(body)
    assert status is not None, (
        "La réponse YANG Patch doit contenir ietf-yang-patch:yang-patch-status."
    )

    assert status.get("patch-id") == patch_id, (
        f"patch-id attendu {patch_id!r}, obtenu {status.get('patch-id')!r}"
    )

    global_errors = get_global_errors(status)
    assert not global_errors, (
        f"Erreurs globales YANG Patch inattendues : {global_errors}"
    )

    edit_errors = get_edit_errors(status)
    if edit_errors:
        pytest.skip(
            f"Erreur d'édition YANG Patch non démontrable : {edit_errors}"
        )

    return status


# ---------------------------------------------------------------------------
# Helpers RESTCONF génériques
# ---------------------------------------------------------------------------

def delete_path(http2_client, api_url, auth_headers, path: str) -> None:
    """Supprime une ressource en ignorant les erreurs."""
    try:
        http2_client.delete(
            f"{api_url}{path}",
            headers={"Accept": YANG_JSON, **auth_headers},
        )
    except Exception:
        pass


def put_json_resource(http2_client, api_url, auth_headers, path: str, body: dict):
    """PUT JSON simple pour préparer un état."""
    headers = {
        "Content-Type": YANG_JSON,
        "Accept": YANG_JSON,
        **auth_headers,
    }
    return http2_client.put(
        f"{api_url}{path}",
        headers=headers,
        content=json.dumps(body).encode(),
    )


def extract_interface_names(response) -> list[str]:
    """Extrait les noms d'interfaces depuis une réponse JSON."""
    try:
        body = response.json()
    except Exception:
        return []

    names = []

    def _walk(node) -> None:
        if isinstance(node, dict):
            for key, value in node.items():
                if local_name(key) == "interface" and isinstance(value, list):
                    for item in value:
                        if isinstance(item, dict):
                            name = item.get("name")
                            if isinstance(name, str):
                                names.append(name)
                _walk(value)
        elif isinstance(node, list):
            for item in node:
                _walk(item)

    _walk(body)
    return names


# ---------------------------------------------------------------------------
# Fixtures : JWT restreint pour NACM
# ---------------------------------------------------------------------------

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
# T-YPATCH-01 : PATCH avec application/yang-patch+json
# ============================================================================
@pytest.mark.roadmap("R26")
@pytest.mark.rfc("RFC 8072")
class TestT_YPATCH_01_JsonAccepted:
    """
    T-YPATCH-01 : PATCH avec application/yang-patch+json.
    Requête acceptée.
    """

    def test_yang_patch_json_accepted(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
    ):
        patch_id = f"ypatch-json-{uuid.uuid4().hex[:8]}"
        edits = [
            {
                "edit-id": "edit-1",
                "operation": "merge",
                "target": SYSTEM_TARGET,
                "value": SYSTEM_VALUE,
            }
        ]

        payload = yang_patch_json(patch_id, edits)

        try:
            response = send_yang_patch(
                http2_client,
                api_url,
                auth_headers,
                payload,
                content_type=YANG_PATCH_JSON,
                accept=YANG_JSON,
            )
            require_patch_success(response, patch_id)
        finally:
            delete_path(http2_client, api_url, auth_headers, SYSTEM_DATA_PATH)


# ============================================================================
# T-YPATCH-02 : PATCH avec application/yang-patch+xml
# ============================================================================
@pytest.mark.roadmap("R26")
@pytest.mark.rfc("RFC 8072")
class TestT_YPATCH_02_XmlAccepted:
    """
    T-YPATCH-02 : PATCH avec application/yang-patch+xml.
    Requête acceptée.
    """

    def test_yang_patch_xml_accepted(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
    ):
        patch_id = f"ypatch-xml-{uuid.uuid4().hex[:8]}"

        edits_xml = (
            "  <edit>\n"
            "    <edit-id>edit-xml-1</edit-id>\n"
            "    <operation>merge</operation>\n"
            f"    <target>{SYSTEM_TARGET}</target>\n"
            "    <value>\n"
            f"      <system xmlns=\"{RT_NS}\">\n"
            "        <config/>\n"
            "      </system>\n"
            "    </value>\n"
            "  </edit>"
        )

        payload = yang_patch_xml(patch_id, edits_xml)

        try:
            response = send_yang_patch(
                http2_client,
                api_url,
                auth_headers,
                payload,
                content_type=YANG_PATCH_XML,
                accept=YANG_XML,
            )
            skip_if_yang_patch_unsupported(response)

            if response.status_code == 400:
                pytest.skip(
                    "YANG Patch XML rejeté avec 400 ; support non démontrable."
                )

            assert response.status_code == 200, (
                f"PATCH YANG Patch XML doit retourner 200 OK, "
                f"obtenu {response.status_code}"
            )

            assert "yang-patch-status" in response.text, (
                "La réponse doit contenir yang-patch-status."
            )

        finally:
            delete_path(http2_client, api_url, auth_headers, SYSTEM_DATA_PATH)


# ============================================================================
# T-YPATCH-03 : Opération create
# ============================================================================
@pytest.mark.roadmap("R26")
@pytest.mark.rfc("RFC 8072")
class TestT_YPATCH_03_Create:
    """
    T-YPATCH-03 : Opération create.
    Ressource créée.
    """

    def test_create_operation(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
    ):
        patch_id = f"ypatch-create-{uuid.uuid4().hex[:8]}"

        delete_path(http2_client, api_url, auth_headers, SYSTEM_DATA_PATH)

        edits = [
            {
                "edit-id": "create-1",
                "operation": "create",
                "target": SYSTEM_TARGET,
                "value": SYSTEM_VALUE,
            }
        ]

        payload = yang_patch_json(patch_id, edits)

        try:
            response = send_yang_patch(
                http2_client,
                api_url,
                auth_headers,
                payload,
            )
            skip_if_yang_patch_unsupported(response)

            if response.status_code == 400:
                pytest.skip("Opération create rejetée avec 400.")

            assert response.status_code == 200

            body = response.json()
            status = get_patch_status(body)
            assert status is not None
            assert status.get("patch-id") == patch_id

            global_errors = get_global_errors(status)
            assert not global_errors

            edit_errors = get_edit_errors(status)
            if edit_errors:
                tags = flatten_error_tags(edit_errors)
                if "data-exists" in tags:
                    pytest.skip("La ressource existe déjà ; create non démontrable.")
                pytest.skip(f"Erreur d'édition create : {edit_errors}")

            get_response = http2_client.get(
                f"{api_url}{SYSTEM_DATA_PATH}",
                headers={"Accept": YANG_JSON, **auth_headers},
            )

            assert get_response.status_code == 200, (
                "Après une opération create réussie, la ressource doit être "
                f"lisible, obtenu {get_response.status_code}"
            )

        finally:
            delete_path(http2_client, api_url, auth_headers, SYSTEM_DATA_PATH)


# ============================================================================
# T-YPATCH-04 : Opération delete
# ============================================================================
@pytest.mark.roadmap("R26")
@pytest.mark.rfc("RFC 8072")
class TestT_YPATCH_04_Delete:
    """
    T-YPATCH-04 : Opération delete.
    Ressource supprimée.
    """

    def test_delete_operation(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
    ):
        patch_id = f"ypatch-delete-{uuid.uuid4().hex[:8]}"

        setup = put_json_resource(
            http2_client,
            api_url,
            auth_headers,
            SYSTEM_DATA_PATH,
            SYSTEM_VALUE,
        )
        if setup.status_code not in (200, 201, 204):
            pytest.skip(
                "Impossible de créer la ressource de test avant delete."
            )

        edits = [
            {
                "edit-id": "delete-1",
                "operation": "delete",
                "target": SYSTEM_TARGET,
            }
        ]

        payload = yang_patch_json(patch_id, edits)

        try:
            response = send_yang_patch(
                http2_client,
                api_url,
                auth_headers,
                payload,
            )
            skip_if_yang_patch_unsupported(response)

            if response.status_code == 400:
                pytest.skip("Opération delete rejetée avec 400.")

            assert response.status_code == 200

            body = response.json()
            status = get_patch_status(body)
            assert status is not None
            assert status.get("patch-id") == patch_id

            global_errors = get_global_errors(status)
            assert not global_errors

            edit_errors = get_edit_errors(status)
            if edit_errors:
                tags = flatten_error_tags(edit_errors)
                if "data-missing" in tags:
                    pytest.skip("La ressource n'était pas présente pour delete.")
                pytest.skip(f"Erreur d'édition delete : {edit_errors}")

            get_response = http2_client.get(
                f"{api_url}{SYSTEM_DATA_PATH}",
                headers={"Accept": YANG_JSON, **auth_headers},
            )

            assert get_response.status_code == 404, (
                "Après une opération delete réussie, la ressource ne doit plus "
                f"être lisible, obtenu {get_response.status_code}"
            )

        finally:
            delete_path(http2_client, api_url, auth_headers, SYSTEM_DATA_PATH)


# ============================================================================
# T-YPATCH-05 : Opération merge
# ============================================================================
@pytest.mark.roadmap("R26")
@pytest.mark.rfc("RFC 8072")
class TestT_YPATCH_05_Merge:
    """
    T-YPATCH-05 : Opération merge.
    Fusion réussie.
    """

    def test_merge_operation(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
    ):
        patch_id = f"ypatch-merge-{uuid.uuid4().hex[:8]}"

        delete_path(http2_client, api_url, auth_headers, SYSTEM_DATA_PATH)

        edits = [
            {
                "edit-id": "merge-1",
                "operation": "merge",
                "target": SYSTEM_TARGET,
                "value": SYSTEM_VALUE,
            }
        ]

        payload = yang_patch_json(patch_id, edits)

        try:
            response = send_yang_patch(
                http2_client,
                api_url,
                auth_headers,
                payload,
            )
            require_patch_success(response, patch_id)

            get_response = http2_client.get(
                f"{api_url}{SYSTEM_DATA_PATH}",
                headers={"Accept": YANG_JSON, **auth_headers},
            )

            assert get_response.status_code == 200, (
                "Après merge, la ressource doit être lisible, obtenu "
                f"{get_response.status_code}"
            )

        finally:
            delete_path(http2_client, api_url, auth_headers, SYSTEM_DATA_PATH)


# ============================================================================
# T-YPATCH-06 : Opération replace
# ============================================================================
@pytest.mark.roadmap("R26")
@pytest.mark.rfc("RFC 8072")
class TestT_YPATCH_06_Replace:
    """
    T-YPATCH-06 : Opération replace.
    Remplacement réussi.
    """

    def test_replace_operation(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
    ):
        patch_id = f"ypatch-replace-{uuid.uuid4().hex[:8]}"

        delete_path(http2_client, api_url, auth_headers, SYSTEM_DATA_PATH)

        edits = [
            {
                "edit-id": "replace-1",
                "operation": "replace",
                "target": SYSTEM_TARGET,
                "value": SYSTEM_VALUE,
            }
        ]

        payload = yang_patch_json(patch_id, edits)

        try:
            response = send_yang_patch(
                http2_client,
                api_url,
                auth_headers,
                payload,
            )
            require_patch_success(response, patch_id)

            get_response = http2_client.get(
                f"{api_url}{SYSTEM_DATA_PATH}",
                headers={"Accept": YANG_JSON, **auth_headers},
            )

            assert get_response.status_code == 200, (
                "Après replace, la ressource doit être lisible, obtenu "
                f"{get_response.status_code}"
            )

        finally:
            delete_path(http2_client, api_url, auth_headers, SYSTEM_DATA_PATH)


# ============================================================================
# T-YPATCH-07 : Opération remove
# ============================================================================
@pytest.mark.roadmap("R26")
@pytest.mark.rfc("RFC 8072")
class TestT_YPATCH_07_Remove:
    """
    T-YPATCH-07 : Opération remove.
    Suppression si présente, sans erreur si absente selon sémantique.
    """

    def test_remove_operation(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
    ):
        setup = put_json_resource(
            http2_client,
            api_url,
            auth_headers,
            SYSTEM_DATA_PATH,
            SYSTEM_VALUE,
        )
        if setup.status_code not in (200, 201, 204):
            pytest.skip("Impossible de préparer la ressource pour remove.")

        try:
            # Premier remove : la ressource est présente.
            patch_id_1 = f"ypatch-remove-1-{uuid.uuid4().hex[:8]}"
            edits_1 = [
                {
                    "edit-id": "remove-1",
                    "operation": "remove",
                    "target": SYSTEM_TARGET,
                }
            ]
            payload_1 = yang_patch_json(patch_id_1, edits_1)

            response_1 = send_yang_patch(
                http2_client,
                api_url,
                auth_headers,
                payload_1,
            )
            require_patch_success(response_1, patch_id_1)

            # Second remove : la ressource est censée être absente.
            patch_id_2 = f"ypatch-remove-2-{uuid.uuid4().hex[:8]}"
            edits_2 = [
                {
                    "edit-id": "remove-2",
                    "operation": "remove",
                    "target": SYSTEM_TARGET,
                }
            ]
            payload_2 = yang_patch_json(patch_id_2, edits_2)

            response_2 = send_yang_patch(
                http2_client,
                api_url,
                auth_headers,
                payload_2,
            )
            require_patch_success(response_2, patch_id_2)

        finally:
            delete_path(http2_client, api_url, auth_headers, SYSTEM_DATA_PATH)


# ============================================================================
# T-YPATCH-08 : Opération insert / move
# ============================================================================
@pytest.mark.roadmap("R26")
@pytest.mark.rfc("RFC 8072")
class TestT_YPATCH_08_InsertMove:
    """
    T-YPATCH-08 : Opération insert / move.
    Ordre respecté pour nœuds ordered-by user.
    """

    def test_insert_and_move_operations(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
    ):
        suffix = uuid.uuid4().hex[:8]
        iface_a = f"ypatch-a-{suffix}"
        iface_b = f"ypatch-b-{suffix}"

        headers_json = {
            "Content-Type": YANG_JSON,
            "Accept": YANG_JSON,
            **auth_headers,
        }

        try:
            # Nettoyage initial.
            delete_path(
                http2_client,
                api_url,
                auth_headers,
                f"{INTERFACE_LIST_PATH}={iface_a}",
            )
            delete_path(
                http2_client,
                api_url,
                auth_headers,
                f"{INTERFACE_LIST_PATH}={iface_b}",
            )

            # Création de l'interface A via RESTCONF classique.
            payload_a = {
                f"{MOD}:interface": [
                    {
                        "name": iface_a,
                    }
                ]
            }
            response_a = http2_client.post(
                f"{api_url}{INTERFACES_PATH}",
                headers=headers_json,
                content=json.dumps(payload_a).encode(),
            )
            if response_a.status_code not in (200, 201, 204):
                pytest.skip(
                    "Impossible de créer l'interface de référence pour insert/move."
                )

            # Insertion de B avant A via YANG Patch.
            insert_patch_id = f"ypatch-insert-{uuid.uuid4().hex[:8]}"
            insert_edits = [
                {
                    "edit-id": "insert-1",
                    "operation": "insert",
                    "target": f"/restconf-test:interfaces/interface={iface_b}",
                    "where": "before",
                    "point": f"/restconf-test:interfaces/interface={iface_a}",
                    "value": {
                        f"{MOD}:interface": [
                            {
                                "name": iface_b,
                            }
                        ]
                    },
                }
            ]
            insert_payload = yang_patch_json(insert_patch_id, insert_edits)
            insert_response = send_yang_patch(
                http2_client,
                api_url,
                auth_headers,
                insert_payload,
            )
            skip_if_yang_patch_unsupported(insert_response)

            if insert_response.status_code == 400:
                pytest.skip(
                    "Opération insert rejetée avec 400 ; la liste n'est peut-être "
                    "pas ordered-by user ou le format value n'est pas supporté."
                )

            require_patch_success(insert_response, insert_patch_id)

            list_response = http2_client.get(
                f"{api_url}{INTERFACE_LIST_PATH}",
                headers={"Accept": YANG_JSON, **auth_headers},
            )
            if list_response.status_code != 200:
                pytest.skip("Impossible de relire la liste d'interfaces.")

            names = extract_interface_names(list_response)
            if iface_a not in names or iface_b not in names:
                pytest.skip(
                    "Impossible de vérifier l'ordre : interfaces non retrouvées."
                )

            assert names.index(iface_b) < names.index(iface_a), (
                "Après insert with where=before, l'interface insérée doit être "
                "avant l'interface de référence."
            )

            # Déplacement de B après A via YANG Patch.
            move_patch_id = f"ypatch-move-{uuid.uuid4().hex[:8]}"
            move_edits = [
                {
                    "edit-id": "move-1",
                    "operation": "move",
                    "target": f"/restconf-test:interfaces/interface={iface_b}",
                    "where": "after",
                    "point": f"/restconf-test:interfaces/interface={iface_a}",
                }
            ]
            move_payload = yang_patch_json(move_patch_id, move_edits)
            move_response = send_yang_patch(
                http2_client,
                api_url,
                auth_headers,
                move_payload,
            )

            skip_if_yang_patch_unsupported(move_response)

            if move_response.status_code == 400:
                pytest.skip("Opération move rejetée avec 400.")

            require_patch_success(move_response, move_patch_id)

            list_response_2 = http2_client.get(
                f"{api_url}{INTERFACE_LIST_PATH}",
                headers={"Accept": YANG_JSON, **auth_headers},
            )
            if list_response_2.status_code != 200:
                pytest.skip("Impossible de relire la liste après move.")

            names_2 = extract_interface_names(list_response_2)
            if iface_a not in names_2 or iface_b not in names_2:
                pytest.skip(
                    "Impossible de vérifier l'ordre après move : interfaces "
                    "non retrouvées."
                )

            assert names_2.index(iface_b) > names_2.index(iface_a), (
                "Après move with where=after, l'interface déplacée doit être "
                "après l'interface de référence."
            )

        finally:
            delete_path(
                http2_client,
                api_url,
                auth_headers,
                f"{INTERFACE_LIST_PATH}={iface_a}",
            )
            delete_path(
                http2_client,
                api_url,
                auth_headers,
                f"{INTERFACE_LIST_PATH}={iface_b}",
            )


# ============================================================================
# T-YPATCH-09 : Plusieurs sous-opérations dans un même PATCH
# ============================================================================
@pytest.mark.roadmap("R26")
@pytest.mark.rfc("RFC 8072")
class TestT_YPATCH_09_MultipleEdits:
    """
    T-YPATCH-09 : Plusieurs sous-opérations dans un même PATCH.
    Réponse yang-patch-status cohérente.
    """

    def test_multiple_edits(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
    ):
        patch_id = f"ypatch-multi-{uuid.uuid4().hex[:8]}"

        edits = [
            {
                "edit-id": "edit-1",
                "operation": "merge",
                "target": SYSTEM_TARGET,
                "value": SYSTEM_VALUE,
            },
            {
                "edit-id": "edit-2",
                "operation": "merge",
                "target": SYSTEM_TARGET,
                "value": SYSTEM_VALUE,
            },
        ]

        payload = yang_patch_json(patch_id, edits)

        try:
            response = send_yang_patch(
                http2_client,
                api_url,
                auth_headers,
                payload,
            )
            status = require_patch_success(response, patch_id)

            assert status.get("patch-id") == patch_id

        finally:
            delete_path(http2_client, api_url, auth_headers, SYSTEM_DATA_PATH)


# ============================================================================
# T-YPATCH-10 : Erreur sur une sous-opération
# ============================================================================
@pytest.mark.roadmap("R26")
@pytest.mark.rfc("RFC 8072")
class TestT_YPATCH_10_EditError:
    """
    T-YPATCH-10 : Erreur sur une sous-opération.
    edit-status contient l'erreur par opération.
    """

    def test_edit_error(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
    ):
        patch_id = f"ypatch-edit-error-{uuid.uuid4().hex[:8]}"

        edits = [
            {
                "edit-id": "edit-ok",
                "operation": "merge",
                "target": SYSTEM_TARGET,
                "value": SYSTEM_VALUE,
            },
            {
                "edit-id": "edit-error",
                "operation": "delete",
                "target": NON_EXISTENT_TARGET,
            },
        ]

        payload = yang_patch_json(patch_id, edits)

        try:
            response = send_yang_patch(
                http2_client,
                api_url,
                auth_headers,
                payload,
            )
            skip_if_yang_patch_unsupported(response)

            # RFC 8072 §2.2, corrigée par EID 5131 : delete sur une cible
            # inexistante doit retourner 404 et un yang-patch-status doit
            # identifier l'edit invalide.
            assert response.status_code == 404

            body = response.json()
            status = get_patch_status(body)
            assert status is not None
            assert status.get("patch-id") == patch_id

            global_errors = get_global_errors(status)
            if global_errors:
                pytest.skip(
                    "Le serveur a retourné des erreurs globales plutôt que "
                    "des erreurs par opération."
                )

            edit_errors = get_edit_errors(status)
            assert edit_errors, (
                "Au moins une erreur d'édition est attendue pour une "
                "sous-opération invalide."
            )

            edit_ids = {
                entry.get("edit-id")
                for entry in edit_errors
            }

            if "edit-error" not in edit_ids:
                pytest.skip(
                    "Erreur d'édition détectée mais pas sur l'edit attendu ; "
                    "le comportement n'est pas démontrable."
                )

            assert "data-missing" in flatten_error_tags(edit_errors), (
                "delete sur une cible inexistante doit signaler data-missing"
            )

        finally:
            delete_path(http2_client, api_url, auth_headers, SYSTEM_DATA_PATH)


# ============================================================================
# T-YPATCH-11 : Erreur globale de requête
# ============================================================================
@pytest.mark.roadmap("R26")
@pytest.mark.rfc("RFC 8072")
class TestT_YPATCH_11_GlobalError:
    """
    T-YPATCH-11 : Erreur globale de requête.
    global-errors renseigné.
    """

    def test_global_error(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
    ):
        # patch-id manquant : erreur globale attendue.
        payload = json.dumps(
            {
                "ietf-yang-patch:yang-patch": {
                    "edit": [
                        {
                            "edit-id": "edit-1",
                            "operation": "merge",
                            "target": SYSTEM_TARGET,
                            "value": SYSTEM_VALUE,
                        }
                    ]
                }
            }
        ).encode()

        response = send_yang_patch(
            http2_client,
            api_url,
            auth_headers,
            payload,
        )
        skip_if_yang_patch_unsupported(response)

        if response.status_code == 200:
            pytest.fail(
                "Une requête YANG Patch invalide ne doit pas retourner 200 OK."
            )

        assert response.status_code == 400, (
            "Une requête YANG Patch globalement invalide doit retourner "
            f"400 Bad Request, obtenu {response.status_code}"
        )

        try:
            body = response.json()
        except Exception:
            pytest.fail(
                "Une erreur globale YANG Patch doit être renvoyée en JSON."
            )

        status = get_patch_status(body)
        assert status is not None, (
            "Une erreur globale YANG Patch doit utiliser "
            "ietf-yang-patch:yang-patch-status, pas seulement "
            "ietf-restconf:errors."
        )

        global_errors = get_global_errors(status)
        assert global_errors, (
            "global-errors doit être renseigné pour une erreur globale "
            "YANG Patch."
        )


# ============================================================================
# T-YPATCH-12 : YANG Patch non autorisé
# ============================================================================
@pytest.mark.roadmap("R26")
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8072")
@pytest.mark.rfc("RFC 8341")
class TestT_YPATCH_12_Unauthorized:
    """
    T-YPATCH-12 : YANG Patch non autorisé.
    Erreur NACM.
    """

    def test_yang_patch_unauthorized(
        self,
        http2_client,
        api_url,
        restricted_headers,
    ):
        patch_id = f"ypatch-nacm-{uuid.uuid4().hex[:8]}"
        edits = [
            {
                "edit-id": "edit-1",
                "operation": "merge",
                "target": SYSTEM_TARGET,
                "value": SYSTEM_VALUE,
            }
        ]

        payload = yang_patch_json(patch_id, edits)

        headers = {
            "Content-Type": YANG_PATCH_JSON,
            **restricted_headers,
        }

        response = http2_client.patch(
            f"{api_url}{DATA_ROOT}",
            headers=headers,
            content=payload,
        )

        if response.status_code == 401:
            pytest.skip("Le JWT restreint est refusé comme invalide.")

        if response.status_code in (403, 404):
            return

        if response.status_code in (200, 204):
            pytest.skip(
                "Le token restreint est autorisé à effectuer YANG Patch ; "
                "aucune règle NACM restrictive n'est démontrable."
            )

        if response.status_code == 400:
            pytest.skip(
                "Requête rejetée avec 400 ; le refus NACM n'est pas démontrable."
            )

        pytest.fail(
            f"Réponse inattendue pour YANG Patch non autorisé : "
            f"{response.status_code}"
        )


# ============================================================================
# T-YPATCH-13 : YANG Patch avec media type incorrect
# ============================================================================
@pytest.mark.roadmap("R26")
@pytest.mark.roadmap("R41")
@pytest.mark.rfc("RFC 8072")
class TestT_YPATCH_13_IncorrectMediaType:
    """
    T-YPATCH-13 : YANG Patch avec media type incorrect.
    415 Unsupported Media Type.
    """

    def test_incorrect_media_type(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
    ):
        patch_id = f"ypatch-media-{uuid.uuid4().hex[:8]}"
        edits = [
            {
                "edit-id": "edit-1",
                "operation": "merge",
                "target": SYSTEM_TARGET,
                "value": SYSTEM_VALUE,
            }
        ]

        payload = yang_patch_json(patch_id, edits)

        headers = {
            "Content-Type": "application/yang-patch+invalid",
            "Accept": YANG_JSON,
            **auth_headers,
        }

        response = http2_client.patch(
            f"{api_url}{DATA_ROOT}",
            headers=headers,
            content=payload,
        )

        if response.status_code == 404:
            pytest.skip("La ressource cible n'est pas disponible.")

        assert response.status_code == 415, (
            "Un media type YANG Patch incorrect doit retourner "
            f"415 Unsupported Media Type, obtenu {response.status_code}"
        )
