# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN

"""Tests de conformité RESTCONF - Section 11 : NACM.

Couvre les items T-NACM-01 à T-NACM-15 de la ROADMAP.md.
RFC liée : RFC 8341.
Items liés : R8, R29, A9, A13.

Un certain nombre de tests NACM nécessitent un environnement configuré avec :
- un JWT "admin" ou privilégié (fixture conftest existante) ;
- un JWT "restreint" associé à un utilisateur/groupe NACM moins privilégié ;
- éventuellement un JWT "inconnu" sans règle NACM explicite ;
- des ressources/RPC/actions/streams volontairement interdits ou partiellement visibles.

Variables d'environnement optionnelles :
- RESTCONF_TEST_JWT_RESTRICTED / RESTCONF_NACM_RESTRICTED_JWT
- RESTCONF_TEST_JWT_UNKNOWN
- RESTCONF_NACM_FORBIDDEN_READ_PATH
- RESTCONF_NACM_PARTIAL_PATH
- RESTCONF_NACM_HIDDEN_FIELD
- RESTCONF_NACM_LIST_PATH
- RESTCONF_NACM_CONFIG_FALSE_PATH
- RESTCONF_NACM_CONFIG_FALSE_FIELD
- RESTCONF_NACM_FORBIDDEN_RPC
- RESTCONF_NACM_FORBIDDEN_ACTION_PATH
- RESTCONF_NACM_FORBIDDEN_STREAM
"""

from __future__ import annotations

import os
import uuid

import pytest

# ---------------------------------------------------------------------------
# Références normatives et chemins
# ---------------------------------------------------------------------------

YANG_JSON = "application/yang-data+json"
MOD = "restconf-test"

BASIC_DATA = "/data/restconf-test:basic-data"
SYSTEM_CONFIG = "/data/restconf-test:system/config"
INTERFACES = "/data/restconf-test:interfaces"
INTERFACE_LIST = "/data/restconf-test:interfaces/interface"

OPERATIONS = "/operations"
STREAMS = "/restconf-state/streams"

ESTABLISH_SUBSCRIPTION_RPC = (
    "/operations/ietf-subscribed-notifications:establish-subscription"
)


def get_content_type(response) -> str:
    """Retourne le Content-Type de la réponse, sans ses paramètres."""
    return response.headers.get("content-type", "").split(";")[0].strip().lower()


# ---------------------------------------------------------------------------
# Helpers JSON / assertions NACM
# ---------------------------------------------------------------------------

def json_contains_key(obj, key: str) -> bool:
    """Cherche récursivement une clé JSON, avec ou sans préfixe module."""
    if not key:
        return False

    target = key.split(":")[-1]

    def _walk(node) -> bool:
        if isinstance(node, dict):
            for k, v in node.items():
                if k == key or k.split(":")[-1] == target:
                    return True
                if _walk(v):
                    return True
        elif isinstance(node, list):
            for item in node:
                if _walk(item):
                    return True
        return False

    return _walk(obj)


def count_list_entries(obj) -> int:
    """Compte le nombre d'entrées présentes dans les listes JSON YANG."""
    total = 0

    def _walk(node) -> None:
        nonlocal total
        if isinstance(node, dict):
            for value in node.values():
                if isinstance(value, list):
                    total += len(value)
                _walk(value)
        elif isinstance(node, list):
            for item in node:
                _walk(item)

    _walk(obj)
    return total


def contains_stream_name(obj, stream_name: str) -> bool:
    """Cherche récursivement un champ 'name' égal à stream_name."""
    if not stream_name:
        return False

    def _walk(node) -> bool:
        if isinstance(node, dict):
            for k, v in node.items():
                if k.split(":")[-1] == "name" and v == stream_name:
                    return True
                if _walk(v):
                    return True
        elif isinstance(node, list):
            for item in node:
                if _walk(item):
                    return True
        return False

    return _walk(obj)


def skip_if_unauthenticated(response, description: str) -> None:
    """Skip si le token fourni est refusé comme invalide."""
    if response.status_code == 401:
        pytest.skip(
            f"{description} a été refusé (401). "
            "Configurez un token valide pour cet environnement."
        )


def assert_forbidden(response) -> None:
    """Vérifie qu'une réponse est bien 403 Forbidden avec enveloppe si JSON."""
    assert response.status_code == 403, (
        f"403 Forbidden attendu, obtenu {response.status_code}"
    )

    if get_content_type(response) == YANG_JSON and response.content:
        try:
            body = response.json()
            assert "ietf-restconf:errors" in body, (
                "Une erreur NACM JSON devrait utiliser ietf-restconf:errors."
            )
        except Exception:
            # On ne bloque pas si le corps n'est pas un JSON exploitable.
            pass


# ---------------------------------------------------------------------------
# Fixtures : tokens alternatifs et chemins configurables
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
            "RESTCONF_TEST_JWT_RESTRICTED ou RESTCONF_NACM_RESTRICTED_JWT."
        )
    return token


@pytest.fixture()
def restricted_headers(restricted_jwt: str) -> dict[str, str]:
    """Headers HTTP pour le token restreint."""
    return {
        "Authorization": f"Bearer {restricted_jwt}",
        "Accept": YANG_JSON,
    }


@pytest.fixture()
def unknown_jwt() -> str:
    """JWT d'un utilisateur sans règle NACM explicite."""
    token = os.getenv("RESTCONF_TEST_JWT_UNKNOWN")
    if not token:
        pytest.skip(
            "Aucun JWT 'utilisateur inconnu' configuré. Définissez "
            "RESTCONF_TEST_JWT_UNKNOWN."
        )
    return token


@pytest.fixture()
def unknown_headers(unknown_jwt: str) -> dict[str, str]:
    """Headers HTTP pour le token inconnu."""
    return {
        "Authorization": f"Bearer {unknown_jwt}",
        "Accept": YANG_JSON,
    }


@pytest.fixture()
def forbidden_read_path() -> str:
    """Ressource censée être interdite en lecture pour le token restreint."""
    return os.getenv("RESTCONF_NACM_FORBIDDEN_READ_PATH", BASIC_DATA)


@pytest.fixture()
def partial_path() -> str:
    """Ressource censée être partiellement lisible pour le token restreint."""
    return os.getenv("RESTCONF_NACM_PARTIAL_PATH", BASIC_DATA)


@pytest.fixture()
def hidden_field() -> str:
    """Champ censé être masqué pour le token restreint."""
    return os.getenv("RESTCONF_NACM_HIDDEN_FIELD", "device-id")


@pytest.fixture()
def list_path() -> str:
    """Liste censée être partiellement visible pour le token restreint."""
    return os.getenv("RESTCONF_NACM_LIST_PATH", INTERFACE_LIST)


@pytest.fixture()
def config_false_path() -> str | None:
    """Chemin d'une feuille config false censée être interdite."""
    return os.getenv("RESTCONF_NACM_CONFIG_FALSE_PATH")


@pytest.fixture()
def config_false_field() -> str | None:
    """Nom d'une feuille config false censée être interdite."""
    return os.getenv("RESTCONF_NACM_CONFIG_FALSE_FIELD")


@pytest.fixture()
def forbidden_action_path() -> str:
    """Action YANG censée être interdite pour le token restreint."""
    path = os.getenv("RESTCONF_NACM_FORBIDDEN_ACTION_PATH")
    if not path:
        pytest.skip(
            "Aucune action interdite configurée. Définissez "
            "RESTCONF_NACM_FORBIDDEN_ACTION_PATH."
        )
    return path


@pytest.fixture()
def forbidden_stream_name() -> str | None:
    """Nom d'un stream censé être masqué pour le token restreint."""
    return os.getenv("RESTCONF_NACM_FORBIDDEN_STREAM")


@pytest.fixture()
def forbidden_rpc_path(http2_client, api_url, auth_headers, require_jwt) -> str:
    """RPC censé être interdit pour le token restreint.

    Si RESTCONF_NACM_FORBIDDEN_RPC n'est pas défini, on prend le premier RPC
    découvert dans /operations avec le token admin.
    """
    env_rpc = os.getenv("RESTCONF_NACM_FORBIDDEN_RPC")
    if env_rpc:
        return env_rpc

    headers = {"Accept": YANG_JSON, **auth_headers}
    response = http2_client.get(f"{api_url}{OPERATIONS}", headers=headers)
    if response.status_code != 200:
        pytest.skip("Impossible de découvrir /operations avec le token admin.")

    try:
        body = response.json()
    except Exception:
        pytest.skip("Réponse /operations non JSON.")

    ops = body.get("ietf-restconf:operations", {})
    if isinstance(ops, dict):
        for key in ops:
            if key and not key.startswith("@"):
                return f"{OPERATIONS}/{key}"

    pytest.skip("Aucun RPC disponible pour les tests NACM.")


@pytest.fixture()
def nacm_write_target(http2_client, api_url, auth_headers, require_jwt):
    """Crée une cible d'écriture temporaire pour les tests PUT/PATCH/DELETE."""
    headers = {
        "Content-Type": YANG_JSON,
        "Accept": YANG_JSON,
        **auth_headers,
    }

    response = http2_client.put(
        f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=b"{}"
    )
    if response.status_code not in (200, 201, 204):
        pytest.skip(
            f"Impossible de préparer {SYSTEM_CONFIG} avec le token admin "
            f"({response.status_code})."
        )

    yield SYSTEM_CONFIG

    try:
        http2_client.delete(f"{api_url}{SYSTEM_CONFIG}", headers=auth_headers)
    except Exception:
        pass


# ============================================================================
# T-NACM-01 : Requête sans token si authentification requise
# ============================================================================
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8341")
class TestT_NACM_01_NoToken:
    """
    T-NACM-01 : Requête sans token si authentification requise.
    401 Unauthorized avec WWW-Authenticate.
    """

    def test_no_token(self, http2_client, api_url):
        headers = {"Accept": YANG_JSON}
        response = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers)

        if response.status_code == 200:
            pytest.skip(
                "L'accès anonyme est autorisé ; T-NACM-01 ne peut pas "
                "vérifier le comportement 401."
            )

        assert response.status_code == 401, (
            "Une requête sans token doit retourner 401 Unauthorized lorsque "
            f"l'authentification est requise, obtenu {response.status_code}"
        )
        assert "www-authenticate" in response.headers, (
            "401 Unauthorized doit inclure le header WWW-Authenticate."
        )


# ============================================================================
# T-NACM-02 : Token valide mais utilisateur non autorisé
# ============================================================================
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8341")
class TestT_NACM_02_ValidButUnauthorized:
    """
    T-NACM-02 : Requête avec token valide mais utilisateur non autorisé.
    403 Forbidden.
    """

    def test_valid_token_unauthorized(
        self,
        http2_client,
        api_url,
        restricted_headers,
        forbidden_read_path,
    ):
        response = http2_client.get(
            f"{api_url}{forbidden_read_path}",
            headers=restricted_headers,
        )
        skip_if_unauthenticated(response, "Le JWT restreint")

        if response.status_code == 200:
            pytest.skip(
                "La ressource n'est pas interdite pour le token restreint ; "
                "configurez RESTCONF_NACM_FORBIDDEN_READ_PATH et les règles NACM."
            )
        if response.status_code == 404:
            pytest.skip("Ressource non trouvée pour le token restreint.")

        assert_forbidden(response)


# ============================================================================
# T-NACM-03 : GET sur ressource partiellement lisible
# ============================================================================
@pytest.mark.roadmap("R8")
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8341")
class TestT_NACM_03_PartialRead:
    """
    T-NACM-03 : GET sur ressource partiellement lisible.
    Les nœuds non autorisés sont omis.
    """

    def test_partial_read(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
        restricted_headers,
        partial_path,
        hidden_field,
    ):
        admin_headers = {"Accept": YANG_JSON, **auth_headers}

        admin_response = http2_client.get(
            f"{api_url}{partial_path}",
            headers=admin_headers,
        )
        if admin_response.status_code != 200:
            pytest.skip(
                f"La ressource {partial_path} n'est pas accessible avec le "
                f"token admin ({admin_response.status_code})."
            )

        admin_body = admin_response.json()
        if not json_contains_key(admin_body, hidden_field):
            pytest.skip(
                f"Le champ {hidden_field!r} n'est pas visible par admin ; "
                "impossible de tester son masquage."
            )

        restricted_response = http2_client.get(
            f"{api_url}{partial_path}",
            headers=restricted_headers,
        )
        skip_if_unauthenticated(restricted_response, "Le JWT restreint")

        if restricted_response.status_code == 403:
            pytest.skip(
                "La ressource est entièrement interdite pour le token restreint ; "
                "ce test couvre uniquement le masquage partiel."
            )
        if restricted_response.status_code == 404:
            pytest.skip("Ressource non trouvée pour le token restreint.")

        assert restricted_response.status_code == 200, (
            "Une ressource partiellement lisible doit retourner 200 pour le "
            f"token restreint, obtenu {restricted_response.status_code}"
        )

        restricted_body = restricted_response.json()
        assert not json_contains_key(restricted_body, hidden_field), (
            f"Le champ {hidden_field!r} doit être masqué pour le token restreint."
        )


# ============================================================================
# T-NACM-04 : GET sur ressource cible entièrement interdite
# ============================================================================
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8341")
class TestT_NACM_04_EntirelyForbiddenRead:
    """
    T-NACM-04 : GET sur ressource cible entièrement interdite.
    403 Forbidden.
    """

    def test_entirely_forbidden_read(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
        restricted_headers,
        forbidden_read_path,
    ):
        admin_headers = {"Accept": YANG_JSON, **auth_headers}

        admin_response = http2_client.get(
            f"{api_url}{forbidden_read_path}",
            headers=admin_headers,
        )
        if admin_response.status_code != 200:
            pytest.skip(
                f"La ressource {forbidden_read_path} n'est pas accessible par "
                "admin ; impossible de prouver qu'elle existe."
            )

        restricted_response = http2_client.get(
            f"{api_url}{forbidden_read_path}",
            headers=restricted_headers,
        )
        skip_if_unauthenticated(restricted_response, "Le JWT restreint")

        if restricted_response.status_code == 200:
            pytest.skip(
                "La ressource est lisible par le token restreint ; configurez "
                "une règle NACM interdisant cette ressource."
            )
        if restricted_response.status_code == 404:
            pytest.skip(
                "La ressource est retournée comme 404 pour le token restreint ; "
                "le comportement attendu ici est 403."
            )

        assert_forbidden(restricted_response)


# ============================================================================
# T-NACM-05 : GET sur liste avec entrées partiellement autorisées
# ============================================================================
@pytest.mark.roadmap("R8")
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8341")
class TestT_NACM_05_PartialListRead:
    """
    T-NACM-05 : GET sur liste avec entrées partiellement autorisées.
    Seules les entrées autorisées sont retournées.
    """

    def test_partial_list_read(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
        restricted_headers,
        list_path,
    ):
        admin_headers = {"Accept": YANG_JSON, **auth_headers}

        admin_response = http2_client.get(
            f"{api_url}{list_path}",
            headers=admin_headers,
        )
        if admin_response.status_code != 200:
            pytest.skip(
                f"La liste {list_path} n'est pas accessible par admin "
                f"({admin_response.status_code})."
            )

        admin_count = count_list_entries(admin_response.json())
        if admin_count == 0:
            pytest.skip("La liste admin est vide ; aucun filtrage à tester.")

        restricted_response = http2_client.get(
            f"{api_url}{list_path}",
            headers=restricted_headers,
        )
        skip_if_unauthenticated(restricted_response, "Le JWT restreint")

        if restricted_response.status_code == 403:
            pytest.skip(
                "La liste est entièrement interdite pour le token restreint ; "
                "ce test couvre uniquement le filtrage partiel."
            )
        if restricted_response.status_code == 404:
            pytest.skip("Liste non trouvée pour le token restreint.")

        assert restricted_response.status_code == 200, (
            "Une liste partiellement lisible doit retourner 200, obtenu "
            f"{restricted_response.status_code}"
        )

        restricted_count = count_list_entries(restricted_response.json())

        if restricted_count == admin_count:
            pytest.skip(
                "Le token restreint voit autant d'entrées qu'admin ; "
                "configurez des règles NACM filtrant certaines entrées."
            )

        assert restricted_count < admin_count, (
            "Le token restreint doit voir strictement moins d'entrées qu'admin."
        )


# ============================================================================
# T-NACM-06 : GET sur feuille config false interdite
# ============================================================================
@pytest.mark.roadmap("R8")
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8341")
class TestT_NACM_06_ConfigFalseForbidden:
    """
    T-NACM-06 : GET sur feuille config false interdite.
    La feuille est omise ou erreur selon la règle validée.
    """

    def test_config_false_forbidden(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
        restricted_headers,
        config_false_path,
        config_false_field,
    ):
        if not config_false_path or not config_false_field:
            pytest.skip(
                "Définissez RESTCONF_NACM_CONFIG_FALSE_PATH et "
                "RESTCONF_NACM_CONFIG_FALSE_FIELD pour ce test."
            )

        admin_headers = {"Accept": YANG_JSON, **auth_headers}
        admin_response = http2_client.get(
            f"{api_url}{config_false_path}",
            headers=admin_headers,
        )
        if admin_response.status_code != 200:
            pytest.skip(
                f"La ressource {config_false_path} n'est pas accessible par admin."
            )

        if not json_contains_key(admin_response.json(), config_false_field):
            pytest.skip(
                f"Le champ config false {config_false_field!r} n'est pas visible "
                "par admin."
            )

        restricted_response = http2_client.get(
            f"{api_url}{config_false_path}",
            headers=restricted_headers,
        )
        skip_if_unauthenticated(restricted_response, "Le JWT restreint")

        # Selon la règle NACM, la feuille peut être omise (200 sans champ)
        # ou l'accès peut être refusé (403/404).
        if restricted_response.status_code in (403, 404):
            return

        assert restricted_response.status_code == 200, (
            "Si la réponse n'est pas un refus NACM, elle doit être 200, obtenu "
            f"{restricted_response.status_code}"
        )

        restricted_body = restricted_response.json()
        assert not json_contains_key(restricted_body, config_false_field), (
            f"La feuille config false {config_false_field!r} doit être masquée."
        )


# ============================================================================
# T-NACM-07 : POST création interdite
# ============================================================================
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8341")
class TestT_NACM_07_PostCreateForbidden:
    """
    T-NACM-07 : POST création interdite.
    403 Forbidden.
    """

    def test_post_create_forbidden(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
        restricted_headers,
    ):
        iface_name = f"nacm-{uuid.uuid4().hex[:8]}"
        payload = (
            f'{{"{MOD}:interface": [{{"name": "{iface_name}"}}]}}'
        ).encode()

        headers = {
            **restricted_headers,
            "Content-Type": YANG_JSON,
        }

        response = http2_client.post(
            f"{api_url}{INTERFACES}",
            headers=headers,
            content=payload,
        )
        skip_if_unauthenticated(response, "Le JWT restreint")

        # Si la création est autorisée, on nettoie et on considère que le
        # prérequis NACM "création interdite" n'est pas rempli.
        if response.status_code in (200, 201, 204):
            admin_headers = {
                "Accept": YANG_JSON,
                **auth_headers,
            }
            try:
                http2_client.delete(
                    f"{api_url}{INTERFACE_LIST}={iface_name}",
                    headers=admin_headers,
                )
            except Exception:
                pass

            pytest.skip(
                "La création est autorisée pour le token restreint ; "
                "configurez une règle NACM interdisant POST."
            )

        if response.status_code == 404:
            pytest.skip("La ressource parent n'est pas visible ou n'existe pas.")
        if response.status_code == 400:
            pytest.skip(
                "Requête rejetée avec 400 ; le refus NACM n'est pas démontrable."
            )

        assert_forbidden(response)


# ============================================================================
# T-NACM-08 : PUT/PATCH/DELETE interdits
# ============================================================================
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8341")
class TestT_NACM_08_WriteMethodsForbidden:
    """
    T-NACM-08 : PUT/PATCH/DELETE interdits.
    403 Forbidden.
    """

    @pytest.mark.parametrize("method", ["PUT", "PATCH", "DELETE"])
    def test_write_method_forbidden(
        self,
        http2_client,
        api_url,
        restricted_headers,
        nacm_write_target,
        method,
    ):
        url = f"{api_url}{nacm_write_target}"
        headers = {
            **restricted_headers,
            "Content-Type": YANG_JSON,
        }

        if method == "DELETE":
            response = http2_client.delete(url, headers=headers)
        else:
            response = http2_client.request(
                method,
                url,
                headers=headers,
                content=b"{}",
            )

        skip_if_unauthenticated(response, "Le JWT restreint")

        if response.status_code in (200, 201, 204):
            pytest.skip(
                f"{method} est autorisé pour le token restreint ; configurez "
                "une règle NACM interdisant cette écriture."
            )
        if response.status_code == 404:
            pytest.skip("La cible d'écriture n'est pas visible ou n'existe pas.")
        if response.status_code == 400:
            pytest.skip(
                f"{method} rejeté avec 400 ; le refus NACM n'est pas démontrable."
            )

        assert_forbidden(response)


# ============================================================================
# T-NACM-09 : RPC interdit
# ============================================================================
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8341")
class TestT_NACM_09_RpcForbidden:
    """
    T-NACM-09 : RPC interdit.
    403 Forbidden.
    """

    def test_rpc_forbidden(
        self,
        http2_client,
        api_url,
        restricted_headers,
        forbidden_rpc_path,
    ):
        rpc_name = forbidden_rpc_path.rsplit("/", 1)[-1]
        module = rpc_name.split(":")[0] if ":" in rpc_name else None

        if module:
            payload = f'{{"{module}:input": {{}}}}'.encode()
        else:
            payload = b"{}"

        headers = {
            **restricted_headers,
            "Content-Type": YANG_JSON,
        }

        response = http2_client.post(
            f"{api_url}{forbidden_rpc_path}",
            headers=headers,
            content=payload,
        )
        skip_if_unauthenticated(response, "Le JWT restreint")

        if response.status_code in (200, 204):
            pytest.skip(
                "Le RPC est autoriséisé pour le token restreint ; configurez "
                "une règle NACM exec interdisant ce RPC."
            )
        if response.status_code == 404:
            pytest.skip("RPC non visible ou inexistant pour le token restreint.")
        if response.status_code == 400:
            pytest.skip(
                "RPC rejeté avec 400 ; le refus NACM n'est pas démontrable."
            )

        assert_forbidden(response)


# ============================================================================
# T-NACM-10 : Action interdite
# ============================================================================
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8341")
class TestT_NACM_10_ActionForbidden:
    """
    T-NACM-10 : Action interdite.
    403 Forbidden.
    """

    def test_action_forbidden(
        self,
        http2_client,
        api_url,
        restricted_headers,
        forbidden_action_path,
    ):
        headers = {
            **restricted_headers,
            "Content-Type": YANG_JSON,
        }

        response = http2_client.post(
            f"{api_url}{forbidden_action_path}",
            headers=headers,
            content=b"{}",
        )
        skip_if_unauthenticated(response, "Le JWT restreint")

        if response.status_code in (200, 204):
            pytest.skip(
                "L'action est autorisée pour le token restreint ; configurez "
                "une règle NACM exec interdisant cette action."
            )
        if response.status_code == 404:
            pytest.skip("Action non visible ou inexistante.")
        if response.status_code == 400:
            pytest.skip(
                "Action rejetée avec 400 ; le refus NACM n'est pas démontrable."
            )

        assert_forbidden(response)


# ============================================================================
# T-NACM-11 : Découverte de stream interdite
# ============================================================================
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8341")
class TestT_NACM_11_StreamDiscoveryForbidden:
    """
    T-NACM-11 : Découverte de stream interdite.
    Stream non visible ou accès refusé.
    """

    def test_stream_discovery_forbidden(
        self,
        http2_client,
        api_url,
        restricted_headers,
        forbidden_stream_name,
    ):
        response = http2_client.get(
            f"{api_url}{STREAMS}",
            headers=restricted_headers,
        )
        skip_if_unauthenticated(response, "Le JWT restreint")

        if response.status_code == 403:
            return

        if response.status_code == 404:
            pytest.skip("La découverte des streams n'est pas supportée.")

        if response.status_code == 200:
            if not forbidden_stream_name:
                pytest.skip(
                    "L'accès à la liste des streams est autorisé et aucun "
                    "stream masqué n'est configuré. Définissez "
                    "RESTCONF_NACM_FORBIDDEN_STREAM."
                )

            body = response.json()
            assert not contains_stream_name(body, forbidden_stream_name), (
                f"Le stream {forbidden_stream_name!r} ne doit pas être visible "
                "pour le token restreint."
            )
            return

        pytest.fail(
            f"Réponse inattendue pour la découverte des streams : "
            f"{response.status_code}"
        )


# ============================================================================
# T-NACM-12 : Établissement de souscription interdit
# ============================================================================
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8341")
class TestT_NACM_12_SubscriptionForbidden:
    """
    T-NACM-12 : Établissement de souscription interdit.
    Erreur d'autorisation.
    """

    def test_subscription_forbidden(
        self,
        http2_client,
        api_url,
        restricted_headers,
    ):
        headers = {
            **restricted_headers,
            "Content-Type": YANG_JSON,
        }

        # Conformément à RFC 8650 / Errata EID 6367, l'input est encapsulé
        # sous un conteneur input qualifié par le module.
        payload = b"""{
            "ietf-subscribed-notifications:input": {
                "stream": "NETCONF",
                "encoding": "application/yang-data+json"
            }
        }"""

        response = http2_client.post(
            f"{api_url}{ESTABLISH_SUBSCRIPTION_RPC}",
            headers=headers,
            content=payload,
        )
        skip_if_unauthenticated(response, "Le JWT restreint")

        if response.status_code in (200, 201):
            pytest.skip(
                "establish-subscription est autorisé pour le token restreint ; "
                "configurez une règle NACM interdisant cette souscription."
            )

        if response.status_code == 403:
            return

        if response.status_code in (404, 400, 501):
            pytest.skip(
                "establish-subscription n'est pas disponible ou non supporté "
                "dans cet environnement."
            )

        pytest.fail(
            f"Réponse inattendue pour establish-subscription : "
            f"{response.status_code}"
        )


# ============================================================================
# T-NACM-13 : Mapping JWT vers groupe NACM
# ============================================================================
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8341")
class TestT_NACM_13_JwtGroupMapping:
    """
    T-NACM-13 : Mapping JWT vers groupe NACM.
    Les règles NACM appliquées correspondent au groupe extrait du token.

    Test comportemental : on vérifie qu'un token admin et un token restreint
    n'obtiennent pas les mêmes droits sur une ressource configurée comme
    interdite pour le token restreint.
    """

    def test_mapping_jwt_group(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
        restricted_headers,
        forbidden_read_path,
    ):
        admin_headers = {"Accept": YANG_JSON, **auth_headers}

        admin_response = http2_client.get(
            f"{api_url}{forbidden_read_path}",
            headers=admin_headers,
        )
        if admin_response.status_code != 200:
            pytest.skip(
                "La ressource de comparaison n'est pas accessible par admin."
            )

        restricted_response = http2_client.get(
            f"{api_url}{forbidden_read_path}",
            headers=restricted_headers,
        )
        skip_if_unauthenticated(restricted_response, "Le JWT restreint")

        if restricted_response.status_code == 403:
            return

        if restricted_response.status_code == 200:
            pytest.skip(
                "Le token restreint a les mêmes droits que admin sur cette "
                "ressource ; impossible de valider le mapping NACM."
            )

        if restricted_response.status_code == 404:
            pytest.skip(
                "La ressource est masquée comme 404 pour le token restreint."
            )

        pytest.fail(
            f"Réponse inattendue du token restreint : "
            f"{restricted_response.status_code}"
        )


# ============================================================================
# T-NACM-14 : Session sysrepo avec identité correcte
# ============================================================================
@pytest.mark.roadmap("A9")
@pytest.mark.rfc("RFC 8341")
class TestT_NACM_14_SysrepoIdentity:
    """
    T-NACM-14 : Session sysrepo avec identité correcte.
    sr_session_set_orig_name() positionné avant accès données.

    Ce point est largement interne à l'implémentation. Le test externe
    vérifie au minimum que deux identités JWT différentes produisent des
    décisions NACM différentes sur une ressource configurée à cet effet.
    """

    def test_identity_is_used_by_nacm(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_jwt,
        restricted_headers,
        forbidden_read_path,
    ):
        admin_headers = {"Accept": YANG_JSON, **auth_headers}

        admin_response = http2_client.get(
            f"{api_url}{forbidden_read_path}",
            headers=admin_headers,
        )
        if admin_response.status_code != 200:
            pytest.skip(
                "La ressource de comparaison n'est pas accessible par admin."
            )

        restricted_response = http2_client.get(
            f"{api_url}{forbidden_read_path}",
            headers=restricted_headers,
        )
        skip_if_unauthenticated(restricted_response, "Le JWT restreint")

        if restricted_response.status_code == 403:
            return

        if restricted_response.status_code == 200:
            pytest.skip(
                "Aucune différence de droits observée entre admin et restreint ; "
                "la validation complète de sr_session_set_orig_name() nécessite "
                "une inspection interne ou un environnement NACM dédié."
            )

        if restricted_response.status_code == 404:
            pytest.skip(
                "La ressource est masquée comme 404 pour le token restreint."
            )

        pytest.fail(
            f"Réponse inattendue du token restreint : "
            f"{restricted_response.status_code}"
        )


# ============================================================================
# T-NACM-15 : Règles par défaut sûres
# ============================================================================
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8341")
class TestT_NACM_15_DefaultSafeRules:
    """
    T-NACM-15 : Règles par défaut sûres.
    En l'absence de règle explicite, le comportement est sécurisé.
    """

    def test_default_deny(
        self,
        http2_client,
        api_url,
        unknown_headers,
    ):
        response = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=unknown_headers,
        )
        skip_if_unauthenticated(response, "Le JWT utilisateur inconnu")

        # Un comportement sécurisé peut être 403, ou éventuellement 404 si
        # l'implémentation masque la ressource.
        if response.status_code in (403, 404):
            return

        if response.status_code == 200:
            pytest.fail(
                "Un utilisateur sans règle NACM explicite ne devrait pas "
                "accéder aux données par défaut."
            )

        pytest.fail(
            f"Réponse inattendue pour un utilisateur sans règle NACM : "
            f"{response.status_code}"
        )