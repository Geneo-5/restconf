# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN

"""Tests de conformité RESTCONF - Section 12 : YANG Library et schema.

Couvre les items T-YLIB-01 à T-YLIB-11 de la ROADMAP.md.
RFC liées : RFC 8525, RFC 8040 §3.7.
Items liés : R16, R17, R18, R48, A17.

Certains tests nécessitent un environnement spécifique :
- installation/retrait de module à chaud : RESTCONF_YLIB_HOTPLUG_COMMAND
- notification de changement YANG Library : RESTCONF_YLIB_NOTIFICATION_STREAM_URL
- modules attendus : RESTCONF_YLIB_EXPECTED_MODULES
- JWT restreint pour NACM : RESTCONF_TEST_JWT_RESTRICTED
"""

from __future__ import annotations

import json
import os
import subprocess
import time

import pytest

# ---------------------------------------------------------------------------
# Références normatives et chemins
# ---------------------------------------------------------------------------

YANG_JSON = "application/yang-data+json"
YANG_SCHEMA_MEDIA_TYPE = "application/yang"

# Chemins probables pour la YANG Library.
# RFC 8525 : /yang-library sur le datastore opérationnel.
# RFC 7895 (legacy) : /modules-state.
YANG_LIBRARY_PATHS = (
    "/ds/ietf-datastores:operational/ietf-yang-library:yang-library",
    "/data/ietf-yang-library:yang-library",
    "/data/ietf-yang-library:modules-state",
)

OPERATIONAL_YANG_LIBRARY_PATH = (
    "/ds/ietf-datastores:operational/ietf-yang-library:yang-library"
)

NETCONF_MONITORING_GET_SCHEMA = "/operations/ietf-netconf-monitoring:get-schema"


def get_content_type(response) -> str:
    """Retourne le Content-Type de la réponse, sans ses paramètres."""
    return response.headers.get("content-type", "").split(";")[0].strip().lower()


# ---------------------------------------------------------------------------
# Helpers YANG Library
# ---------------------------------------------------------------------------

def local_name(key: str) -> str:
    """Retourne le nom local d'une clé JSON YANG, sans préfixe module."""
    return key.split(":")[-1]


def get_yang_field(obj: dict, field: str):
    """Récupère un champ d'un objet JSON YANG en ignorant le préfixe."""
    if not isinstance(obj, dict):
        return None

    for key, value in obj.items():
        if local_name(key) == field:
            return value

    return None


def find_content_id(obj) -> str | None:
    """Cherche content-id, ou à défaut module-set-id pour l'ancien modèle."""
    content_ids = []
    legacy_ids = []

    def _walk(node) -> None:
        if isinstance(node, dict):
            for key, value in node.items():
                lname = local_name(key)
                if lname == "content-id" and isinstance(value, str):
                    content_ids.append(value)
                elif lname == "module-set-id" and isinstance(value, str):
                    legacy_ids.append(value)
                _walk(value)
        elif isinstance(node, list):
            for item in node:
                _walk(item)

    _walk(obj)

    if content_ids:
        return content_ids[0]
    if legacy_ids:
        return legacy_ids[0]
    return None


def _looks_like_module_list(items) -> bool:
    """Détermine si une liste JSON ressemble à une liste de modules YANG."""
    if not items or not all(isinstance(item, dict) for item in items):
        return False

    valid = 0
    for item in items:
        name = get_yang_field(item, "name")
        if not name:
            continue

        revision = get_yang_field(item, "revision")
        namespace = get_yang_field(item, "namespace")

        if revision is not None or namespace is not None:
            valid += 1

    return valid > 0


def find_module_entries(obj) -> list[dict]:
    """Extrait les entrées de modules depuis une réponse YANG Library."""
    found = []

    def _walk(node) -> None:
        if isinstance(node, dict):
            for key, value in node.items():
                lname = local_name(key)

                if lname in ("module", "modules") and isinstance(value, list):
                    if _looks_like_module_list(value):
                        found.extend(value)
                    else:
                        _walk(value)
                else:
                    _walk(value)

        elif isinstance(node, list):
            if _looks_like_module_list(node):
                found.extend(node)
            else:
                for item in node:
                    _walk(item)

    _walk(obj)

    # Déduplication par (name, revision).
    unique = []
    seen = set()

    for module in found:
        if not isinstance(module, dict):
            continue

        name = get_yang_field(module, "name")
        if not isinstance(name, str) or not name:
            continue

        revision = get_yang_field(module, "revision")
        revision = revision if isinstance(revision, str) else ""

        key = (name, revision)
        if key in seen:
            continue

        seen.add(key)
        unique.append(module)

    return unique


def get_module_locations(module: dict) -> list[str]:
    """Extrait les locations d'un module YANG."""
    location = get_yang_field(module, "location")

    if location is None:
        return []
    if isinstance(location, str):
        return [location]
    if isinstance(location, list):
        return [item for item in location if isinstance(item, str)]

    return []


def resolve_location(base_url: str, api_url: str, location: str) -> str:
    """Construit une URL interrogeable à partir d'une location YANG Library."""
    if location.startswith(("http://", "https://")):
        return location

    if location.startswith("/"):
        return f"{base_url}{location}"

    return f"{api_url.rstrip('/')}/{location.lstrip('/')}"


def standard_schema_path(name: str, revision: str | None) -> str:
    """Construit le chemin de schéma RESTCONF standard pour un module."""
    if revision:
        return f"/yang/{name}@{revision}.yang"
    return f"/yang/{name}.yang"


def json_has_key(obj, target_field: str) -> bool:
    """Cherche récursivement une clé JSON YANG par nom local."""
    def _walk(node) -> bool:
        if isinstance(node, dict):
            for key, value in node.items():
                if local_name(key) == target_field:
                    return True
                if _walk(value):
                    return True
        elif isinstance(node, list):
            for item in node:
                if _walk(item):
                    return True
        return False

    return _walk(obj)


def fetch_content_id(http2_client, api_url, auth_headers, path) -> str | None:
    """Récupère le content-id courant de la YANG Library."""
    headers = {"Accept": YANG_JSON, **auth_headers}
    response = http2_client.get(f"{api_url}{path}", headers=headers)

    if response.status_code != 200:
        return None

    try:
        body = response.json()
    except Exception:
        return None

    return find_content_id(body)


# ---------------------------------------------------------------------------
# Fixtures : YANG Library
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def yang_library_probe(http2_client, api_url, auth_headers, test_jwt):
    """Sonde les chemins possibles de la YANG Library."""
    if not test_jwt:
        return None

    headers = {"Accept": YANG_JSON, **auth_headers}
    responses = {}

    for path in YANG_LIBRARY_PATHS:
        try:
            responses[path] = http2_client.get(
                f"{api_url}{path}",
                headers=headers,
            )
        except Exception:
            continue

    return responses


@pytest.fixture(scope="session")
def yang_library_selection(yang_library_probe):
    """Choisit la première YANG Library accessible."""
    if yang_library_probe is None:
        pytest.skip("Aucun JWT configuré pour accéder à la YANG Library.")

    if not yang_library_probe:
        pytest.skip("Aucune réponse obtenue pour la YANG Library.")

    # Priorité : opérationnel NMDA, puis /data/yang-library, puis legacy.
    preferred_order = (
        OPERATIONAL_YANG_LIBRARY_PATH,
        "/data/ietf-yang-library:yang-library",
        "/data/ietf-yang-library:modules-state",
    )

    for path in preferred_order:
        response = yang_library_probe.get(path)
        if response is not None and response.status_code == 200:
            return path, response

    for path, response in yang_library_probe.items():
        if response.status_code == 200:
            return path, response

    statuses = {
        path: response.status_code
        for path, response in yang_library_probe.items()
    }

    if all(code == 404 for code in statuses.values()):
        pytest.skip(f"YANG Library non exposée ({statuses}).")

    if all(code in (401, 403) for code in statuses.values()):
        pytest.skip(f"Accès à la YANG Library refusé ({statuses}).")

    pytest.skip(f"YANG Library inaccessible ({statuses}).")


@pytest.fixture(scope="session")
def yang_library_path(yang_library_selection) -> str:
    """Chemin RESTCONF de la YANG Library retenue."""
    return yang_library_selection[0]


@pytest.fixture(scope="session")
def yang_library_response(yang_library_selection):
    """Réponse HTTP de la YANG Library retenue."""
    return yang_library_selection[1]


@pytest.fixture(scope="session")
def yang_modules(yang_library_response) -> list[dict]:
    """Extrait la liste des modules de la YANG Library."""
    try:
        body = yang_library_response.json()
    except Exception:
        pytest.skip("Réponse YANG Library non JSON.")

    modules = find_module_entries(body)
    if not modules:
        pytest.skip("Aucun module trouvé dans la YANG Library.")

    return modules


@pytest.fixture(scope="session")
def module_schema(http2_client, base_url, api_url, auth_headers, yang_modules):
    """Retourne le premier schéma YANG accessible avec son module."""
    headers = {"Accept": YANG_SCHEMA_MEDIA_TYPE, **auth_headers}

    # 1. Essayer les locations fournies par la YANG Library.
    for module in yang_modules:
        for location in get_module_locations(module):
            if location.lower().startswith("file://"):
                continue

            # Pour éviter les dépendances réseau externes, on n'essaie que
            # les locations same-origin ou relatives.
            if location.startswith(("http://", "https://")):
                if not location.startswith(base_url):
                    continue

            url = resolve_location(base_url, api_url, location)

            try:
                response = http2_client.get(url, headers=headers)
            except Exception:
                continue

            if response.status_code == 200:
                return module, url, response

    # 2. Essayer le chemin standard RESTCONF {+restconf}/yang/...
    for module in yang_modules:
        name = get_yang_field(module, "name")
        if not name:
            continue

        revision = get_yang_field(module, "revision")
        path = standard_schema_path(name, revision)
        url = f"{api_url}{path}"

        try:
            response = http2_client.get(url, headers=headers)
        except Exception:
            continue

        if response.status_code == 200:
            return module, url, response

    pytest.skip(
        "Aucun schéma YANG accessible via les locations ou l'endpoint /yang."
    )


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
# T-YLIB-01 : GET sur la YANG Library
# ============================================================================
@pytest.mark.roadmap("R16")
@pytest.mark.rfc("RFC 8525")
class TestT_YLIB_01_GetYangLibrary:
    """
    T-YLIB-01 : GET sur la YANG Library.
    Modules, révisions, features, deviations et content-id sont exposés.
    """

    def test_get_yang_library(
        self,
        yang_library_response,
        yang_library_path,
        yang_modules,
    ):
        assert yang_library_response.status_code == 200
        assert get_content_type(yang_library_response) == YANG_JSON

        try:
            body = yang_library_response.json()
        except Exception:
            pytest.fail("La réponse YANG Library doit être du JSON valide.")

        content_id = find_content_id(body)
        assert content_id is not None, (
            "La YANG Library doit exposer un content-id "
            "(ou un module-set-id pour l'ancien modèle)."
        )

        assert len(yang_modules) > 0, "Au moins un module doit être exposé."

        # Vérification minimale des champs indispensables.
        for module in yang_modules:
            name = get_yang_field(module, "name")
            assert isinstance(name, str) and name, (
                "Chaque module doit avoir un nom."
            )

            revision = get_yang_field(module, "revision")
            if revision is not None:
                assert isinstance(revision, str), (
                    f"La révision du module {name!r} doit être une chaîne."
                )

            # features et deviations sont optionnels mais doivent être
            # structurés s'ils sont présents.
            features = get_yang_field(module, "feature")
            if features is not None:
                assert isinstance(features, (list, str)), (
                    f"feature du module {name!r} doit être une liste ou une chaîne."
                )

            deviations = get_yang_field(module, "deviation")
            if deviations is not None:
                assert isinstance(deviations, (list, str)), (
                    f"deviation du module {name!r} doit être une liste ou une chaîne."
                )


# ============================================================================
# T-YLIB-02 : Exposition sur datastore opérationnel
# ============================================================================
@pytest.mark.roadmap("R16")
@pytest.mark.rfc("RFC 8525")
@pytest.mark.rfc("RFC 8527")
class TestT_YLIB_02_OperationalExposure:
    """
    T-YLIB-02 : Exposition sur datastore opérationnel.
    La YANG Library est lue depuis l'opérationnel.
    """

    def test_operational_exposure(
        self,
        yang_library_probe,
        yang_library_path,
    ):
        operational_response = yang_library_probe.get(OPERATIONAL_YANG_LIBRARY_PATH)

        if operational_response is not None and operational_response.status_code == 200:
            return

        if yang_library_path.startswith("/data/"):
            pytest.skip(
                "La YANG Library est accessible via /data, mais l'exposition "
                "opérationnelle NMDA /ds/ietf-datastores:operational n'est pas "
                "disponible pour le prouver."
            )

        pytest.skip(
            "Impossible de prouver que la YANG Library est exposée sur le "
            "datastore opérationnel."
        )


# ============================================================================
# T-YLIB-03 : Cohérence avec modules chargés
# ============================================================================
@pytest.mark.roadmap("R16")
@pytest.mark.rfc("RFC 8525")
class TestT_YLIB_03_ModuleConsistency:
    """
    T-YLIB-03 : Cohérence avec modules chargés.
    Les modules exposés correspondent à l'état réel sysrepo/libyang.

    En test externe, on vérifie au minimum la cohérence structurelle et,
    si configurée, une liste de modules attendus.
    """

    def test_module_consistency(self, yang_modules):
        names = []
        seen = set()

        for module in yang_modules:
            name = get_yang_field(module, "name")
            assert isinstance(name, str) and name, "Module sans nom."

            revision = get_yang_field(module, "revision")
            revision = revision if isinstance(revision, str) else ""

            key = (name, revision)
            assert key not in seen, (
                f"Module dupliqué dans la YANG Library : {name}@{revision}"
            )
            seen.add(key)
            names.append(name)

        expected_env = os.getenv("RESTCONF_YLIB_EXPECTED_MODULES")
        if expected_env:
            expected = {
                item.strip()
                for item in expected_env.split(",")
                if item.strip()
            }
            missing = expected - set(names)
            assert not missing, (
                f"Modules attendus absents de la YANG Library : {sorted(missing)}"
            )
        else:
            # Sans liste explicite, on vérifie seulement qu'il y a au moins
            # un module cohérent.
            assert len(names) > 0


# ============================================================================
# T-YLIB-04 : URLs location accessibles
# ============================================================================
@pytest.mark.roadmap("R16")
@pytest.mark.roadmap("R17")
@pytest.mark.rfc("RFC 8525")
@pytest.mark.rfc("RFC 8040 §3.7")
class TestT_YLIB_04_LocationsAccessible:
    """
    T-YLIB-04 : URLs location accessibles.
    Les URLs de schéma retournées sont réellement accessibles.
    """

    def test_locations_accessible(
        self,
        http2_client,
        base_url,
        api_url,
        auth_headers,
        yang_modules,
    ):
        headers = {
            "Accept": f"{YANG_SCHEMA_MEDIA_TYPE}, */*",
            **auth_headers,
        }

        attempted = []
        failures = []
        max_attempts = 5

        for module in yang_modules:
            for location in get_module_locations(module):
                if location.lower().startswith("file://"):
                    continue

                if location.startswith(("http://", "https://")):
                    if not location.startswith(base_url):
                        continue
                    url = location
                else:
                    url = resolve_location(base_url, api_url, location)

                attempted.append(url)

                try:
                    response = http2_client.get(url, headers=headers)
                except Exception as exc:
                    failures.append((url, f"exception: {exc}"))
                else:
                    if response.status_code != 200:
                        failures.append((url, response.status_code))

                if len(attempted) >= max_attempts:
                    break

            if len(attempted) >= max_attempts:
                break

        if not attempted:
            pytest.skip(
                "Aucune location HTTP same-origin exploitable dans la YANG Library."
            )

        assert not failures, (
            f"Locations de schéma inaccessibles : {failures}"
        )


# ============================================================================
# T-YLIB-05 : GET sur un module YANG
# ============================================================================
@pytest.mark.roadmap("R17")
@pytest.mark.rfc("RFC 8040 §3.7")
class TestT_YLIB_05_GetModule:
    """
    T-YLIB-05 : GET sur un module YANG.
    Réponse 200 OK avec Content-Type: application/yang.
    """

    def test_get_module(self, module_schema):
        module, url, response = module_schema

        assert response.status_code == 200, (
            f"GET sur le schéma {url} doit retourner 200, "
            f"obtenu {response.status_code}"
        )

        assert get_content_type(response) == YANG_SCHEMA_MEDIA_TYPE, (
            f"Content-Type attendu {YANG_SCHEMA_MEDIA_TYPE!r}, "
            f"obtenu {response.headers.get('content-type')!r}"
        )

        assert len(response.content) > 0, "Le schéma YANG ne doit pas être vide."


# ============================================================================
# T-YLIB-06 : GET sur module inconnu
# ============================================================================
@pytest.mark.roadmap("R17")
@pytest.mark.rfc("RFC 8040 §3.7")
class TestT_YLIB_06_GetUnknownModule:
    """
    T-YLIB-06 : GET sur module inconnu.
    404 Not Found.
    """

    def test_get_unknown_module(
        self,
        http2_client,
        api_url,
        auth_headers,
        yang_modules,
    ):
        headers = {"Accept": YANG_SCHEMA_MEDIA_TYPE, **auth_headers}

        known_url = None

        # On valide d'abord que l'endpoint standard /yang fonctionne.
        for module in yang_modules:
            name = get_yang_field(module, "name")
            if not name:
                continue

            revision = get_yang_field(module, "revision")
            path = standard_schema_path(name, revision)
            url = f"{api_url}{path}"

            try:
                response = http2_client.get(url, headers=headers)
            except Exception:
                continue

            if response.status_code == 200:
                known_url = url
                break

        if known_url is None:
            pytest.skip(
                "L'endpoint standard /yang n'est pas disponible pour tester "
                "un module inconnu."
            )

        unknown_url = f"{api_url}/yang/nonexistent-module@2020-01-01.yang"
        response = http2_client.get(unknown_url, headers=headers)

        assert response.status_code == 404, (
            f"GET sur un module inconnu doit retourner 404, "
            f"obtenu {response.status_code}"
        )


# ============================================================================
# T-YLIB-07 : GET sur module avec révision
# ============================================================================
@pytest.mark.roadmap("R17")
@pytest.mark.rfc("RFC 8040 §3.7")
class TestT_YLIB_07_GetModuleWithRevision:
    """
    T-YLIB-07 : GET sur module avec révision.
    La bonne révision est servie.
    """

    def test_get_module_with_revision(self, module_schema):
        module, url, response = module_schema

        revision = get_yang_field(module, "revision")
        if not revision:
            pytest.skip("Le module sélectionné n'a pas de révision.")

        assert response.status_code == 200
        assert get_content_type(response) == YANG_SCHEMA_MEDIA_TYPE

        body_text = response.text

        # Le code source YANG contient normalement la révision.
        assert revision in body_text, (
            f"La révision {revision!r} n'apparaît pas dans le schéma servi "
            f"depuis {url}."
        )


# ============================================================================
# T-YLIB-08 : Installation/retrait de module à chaud
# ============================================================================
@pytest.mark.roadmap("R16")
@pytest.mark.roadmap("R48")
@pytest.mark.rfc("RFC 8525")
class TestT_YLIB_08_HotplugContentId:
    """
    T-YLIB-08 : Installation/retrait de module à chaud.
    content-id change.

    Ce test nécessite une commande externe pour modifier le set de modules.
    """

    def test_content_id_changes_on_hotplug(
        self,
        http2_client,
        api_url,
        auth_headers,
        yang_library_path,
    ):
        command = os.getenv("RESTCONF_YLIB_HOTPLUG_COMMAND")
        if not command:
            pytest.skip(
                "Définissez RESTCONF_YLIB_HOTPLUG_COMMAND pour tester le "
                "changement de content-id après installation/retrait de module."
            )

        before = fetch_content_id(
            http2_client,
            api_url,
            auth_headers,
            yang_library_path,
        )
        if before is None:
            pytest.skip("Impossible de lire le content-id avant hotplug.")

        timeout = int(os.getenv("RESTCONF_YLIB_HOTPLUG_TIMEOUT", "120"))
        delay = float(os.getenv("RESTCONF_YLIB_HOTPLUG_DELAY", "2.0"))

        try:
            completed = subprocess.run(
                command,
                shell=True,
                timeout=timeout,
                capture_output=True,
                text=True,
            )
        except subprocess.TimeoutExpired:
            pytest.fail(
                f"La commande hotplug a expiré après {timeout}s : {command!r}"
            )

        if completed.returncode != 0:
            pytest.fail(
                f"La commande hotplug a échoué ({completed.returncode}) : "
                f"{completed.stderr or completed.stdout}"
            )

        time.sleep(delay)

        after = fetch_content_id(
            http2_client,
            api_url,
            auth_headers,
            yang_library_path,
        )
        if after is None:
            pytest.fail("Impossible de relire le content-id après hotplug.")

        assert before != after, (
            "Le content-id doit changer après une installation ou un retrait "
            "de module à chaud."
        )


# ============================================================================
# T-YLIB-09 : Notification de changement YANG Library
# ============================================================================
@pytest.mark.roadmap("R48")
@pytest.mark.rfc("RFC 8525")
class TestT_YLIB_09_ChangeNotification:
    """
    T-YLIB-09 : Notification de changement YANG Library si supportée.
    Notification émise lors d'un changement de module set.

    Ce test est optionnel et nécessite :
    - une commande hotplug ;
    - une URL SSE capable de recevoir la notification.
    """

    def test_yang_library_change_notification(
        self,
        http2_client,
        api_url,
        auth_headers,
    ):
        command = os.getenv("RESTCONF_YLIB_HOTPLUG_COMMAND")
        stream_url = os.getenv("RESTCONF_YLIB_NOTIFICATION_STREAM_URL")

        if not command or not stream_url:
            pytest.skip(
                "Définissez RESTCONF_YLIB_HOTPLUG_COMMAND et "
                "RESTCONF_YLIB_NOTIFICATION_STREAM_URL pour tester la "
                "notification de changement YANG Library."
            )

        if not stream_url.startswith(("http://", "https://")):
            stream_url = f"{api_url}{stream_url}"

        headers = {
            "Accept": "text/event-stream",
            **auth_headers,
        }

        received = []

        try:
            with http2_client.stream(
                "GET",
                stream_url,
                headers=headers,
                timeout=15.0,
            ) as response:
                if response.status_code != 200:
                    pytest.skip(
                        f"Le flux SSE {stream_url} n'est pas accessible "
                        f"({response.status_code})."
                    )

                proc = subprocess.Popen(command, shell=True)
                start = time.time()

                try:
                    for line in response.iter_lines():
                        if line:
                            received.append(line)

                        elapsed = time.time() - start
                        if elapsed > 10:
                            break

                        # Laisse quelques secondes après la commande.
                        if proc.poll() is not None and elapsed > 5:
                            break
                finally:
                    try:
                        proc.wait(timeout=5)
                    except Exception:
                        proc.kill()

        except Exception as exc:
            pytest.skip(f"Impossible d'exploiter le flux SSE : {exc}")

        assert any("yang-library" in str(line).lower() for line in received), (
            "Aucune notification YANG Library n'a été observée après le "
            "changement de module set."
        )


# ============================================================================
# T-YLIB-10 : RPC get-schema si supporté
# ============================================================================
@pytest.mark.roadmap("R18")
@pytest.mark.rfc("RFC 8040 §3.6")
class TestT_YLIB_10_GetSchemaRpc:
    """
    T-YLIB-10 : RPC get-schema si supporté.
    Retourne le schéma demandé.
    """

    def test_get_schema_rpc(
        self,
        http2_client,
        api_url,
        auth_headers,
        yang_modules,
    ):
        module = None
        for candidate in yang_modules:
            if get_yang_field(candidate, "name"):
                module = candidate
                break

        if module is None:
            pytest.skip("Aucun module utilisable pour get-schema.")

        identifier = get_yang_field(module, "name")
        revision = get_yang_field(module, "revision")

        input_obj = {"identifier": identifier}
        if revision:
            input_obj["version"] = revision

        payload = json.dumps(
            {
                "ietf-netconf-monitoring:input": input_obj,
            }
        ).encode()

        headers = {
            "Content-Type": YANG_JSON,
            "Accept": YANG_JSON,
            **auth_headers,
        }

        response = http2_client.post(
            f"{api_url}{NETCONF_MONITORING_GET_SCHEMA}",
            headers=headers,
            content=payload,
        )

        if response.status_code == 404:
            pytest.skip("Le RPC get-schema n'est pas exposé.")

        if response.status_code in (401, 403):
            pytest.skip(
                f"Accès au RPC get-schema refusé ({response.status_code})."
            )

        if response.status_code == 400:
            pytest.skip(
                "Le RPC get-schema a rejeté l'input ; le support n'est pas "
                "démontrable dans cet environnement."
            )

        assert response.status_code == 200, (
            f"get-schema doit retourner 200 en cas de succès, "
            f"obtenu {response.status_code}"
        )

        try:
            body = response.json()
        except Exception:
            pytest.fail("La réponse get-schema doit être JSON.")

        assert json_has_key(body, "output"), (
            "La réponse get-schema doit contenir un nœud output."
        )
        assert json_has_key(body, "schema"), (
            "La réponse get-schema doit contenir le champ schema."
        )


# ============================================================================
# T-YLIB-11 : NACM sur YANG Library
# ============================================================================
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8341")
class TestT_YLIB_11_NacmOnYangLibrary:
    """
    T-YLIB-11 : NACM sur YANG Library.
    Accès filtré si des règles s'appliquent.
    """

    def test_nacm_on_yang_library(
        self,
        http2_client,
        api_url,
        auth_headers,
        restricted_headers,
        yang_library_path,
        yang_modules,
    ):
        url = f"{api_url}{yang_library_path}"

        response = http2_client.get(url, headers=restricted_headers)

        if response.status_code == 401:
            pytest.skip("Le JWT restreint est refusé comme invalide.")

        # Accès refusé ou masqué.
        if response.status_code in (403, 404):
            return

        if response.status_code != 200:
            pytest.fail(
                f"Réponse inattendue du token restreint sur la YANG Library : "
                f"{response.status_code}"
            )

        try:
            body = response.json()
        except Exception:
            pytest.fail("La réponse restreinte de la YANG Library doit être JSON.")

        restricted_modules = find_module_entries(body)
        admin_count = len(yang_modules)
        restricted_count = len(restricted_modules)

        if restricted_count < admin_count:
            return

        pytest.skip(
            "Le token restreint voit autant de modules que le token admin ; "
            "aucune règle NACM de filtrage n'est démontrable."
        )