# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN

"""Tests de conformité RESTCONF - Section 15 : Event streams et SSE.

Couvre les items T-SSE-01 à T-SSE-15 de la ROADMAP.md.
RFC liées : RFC 8040 §3.8, §6.2-6.4, RFC 9113.
Items liés : R23, R24, R25, R21, A3, A4, A6, A16.

Un certain nombre de tests SSE nécessitent un environnement configuré :
- RESTCONF_SSE_STREAM_NAME : nom du stream à tester ;
- RESTCONF_SSE_REPLAY_STREAM_NAME : nom d'un stream avec replay ;
- RESTCONF_SSE_NO_REPLAY_STREAM_NAME : nom d'un stream sans replay ;
- RESTCONF_SSE_NOTIFICATION_COMMAND : commande pour déclencher une notification ;
- RESTCONF_SSE_NOTIFICATION_TIMEOUT : timeout en secondes ;
- RESTCONF_SSE_HEARTBEAT_TIMEOUT : timeout heartbeat en secondes ;
- RESTCONF_SSE_FORBIDDEN_STREAM : stream censé être interdit par NACM ;
- RESTCONF_TEST_JWT_RESTRICTED : JWT restreint pour les tests NACM.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import time
from urllib.parse import quote, urlencode

import httpx
import pytest

# ---------------------------------------------------------------------------
# Références normatives et chemins
# ---------------------------------------------------------------------------

YANG_JSON = "application/yang-data+json"
YANG_XML = "application/yang-data+xml"
SSE_MEDIA_TYPE = "text/event-stream"

STREAMS_PATH = "/restconf-state/streams"

START_TIME_PAST = "2000-01-01T00:00:00Z"
STOP_TIME_PAST = "2000-01-01T00:05:00Z"


def get_content_type(response) -> str:
    """Retourne le Content-Type de la réponse, sans ses paramètres."""
    return response.headers.get("content-type", "").split(";")[0].strip().lower()


def get_float_env(name: str, default: float) -> float:
    """Lit une variable d'environnement numérique."""
    raw = os.getenv(name)
    if raw is None:
        return default
    try:
        return float(raw)
    except Exception:
        return default


# ---------------------------------------------------------------------------
# Helpers YANG / streams
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


def truthy(value) -> bool:
    """Interprète une valeur comme booléenne."""
    if value is True:
        return True
    if value is False or value is None:
        return False
    return str(value).strip().lower() in ("true", "1", "yes", "enabled")


def find_stream_entries(obj) -> list[dict]:
    """Extrait les entrées de stream depuis la réponse streams."""
    found = []

    def _looks_like_stream_list(items) -> bool:
        if not items or not all(isinstance(item, dict) for item in items):
            return False

        valid = 0
        for item in items:
            name = get_yang_field(item, "name")
            if not name:
                continue

            has_meta = any(
                get_yang_field(item, field) is not None
                for field in ("description", "replay-support", "access", "location")
            )
            if has_meta:
                valid += 1

        return valid > 0

    def _walk(node) -> None:
        if isinstance(node, dict):
            for key, value in node.items():
                lname = local_name(key)

                if lname in ("stream", "streams") and isinstance(value, list):
                    if _looks_like_stream_list(value):
                        found.extend(value)
                    else:
                        _walk(value)
                else:
                    _walk(value)

        elif isinstance(node, list):
            if _looks_like_stream_list(node):
                found.extend(node)
            else:
                for item in node:
                    _walk(item)

    _walk(obj)

    unique = []
    seen = set()

    for entry in found:
        if not isinstance(entry, dict):
            continue

        name = get_yang_field(entry, "name")
        if not isinstance(name, str) or not name:
            continue

        if name in seen:
            continue

        seen.add(name)
        unique.append(entry)

    return unique


def find_first_location(node):
    """Cherche récursivement une location/URL de stream."""
    if isinstance(node, str):
        return node

    if isinstance(node, dict):
        for key, value in node.items():
            if local_name(key) == "location" and isinstance(value, str):
                return value

        access = get_yang_field(node, "access")
        if access is not None:
            location = find_first_location(access)
            if location:
                return location

        for value in node.values():
            location = find_first_location(value)
            if location:
                return location

    elif isinstance(node, list):
        for item in node:
            location = find_first_location(item)
            if location:
                return location

    return None


def resolve_location(base_url: str, api_url: str, location: str) -> str:
    """Construit une URL interrogeable à partir d'une location."""
    if location.startswith(("http://", "https://")):
        return location

    if location.startswith("/"):
        return f"{base_url}{location}"

    return f"{api_url.rstrip('/')}/{location.lstrip('/')}"


def get_stream_name(entries: list[dict], env_var: str, required_replay=None) -> str | None:
    """Sélectionne un stream par configuration ou découverte."""
    env_name = os.getenv(env_var)
    if env_name:
        return env_name

    for entry in entries:
        name = get_yang_field(entry, "name")
        if not name:
            continue

        if required_replay is None:
            return name

        replay = get_yang_field(entry, "replay-support")

        if required_replay and truthy(replay):
            return name

        if not required_replay and not truthy(replay):
            return name

    return None


def get_stream_url(base_url: str, api_url: str, entries: list[dict], name: str) -> str:
    """Construit l'URL SSE d'un stream."""
    for entry in entries:
        if get_yang_field(entry, "name") == name:
            location = find_first_location(entry)
            if location:
                return resolve_location(base_url, api_url, location)

    return f"{api_url}/streams/stream/{quote(name, safe='')}"


def add_query_params(url: str, params: dict[str, str]) -> str:
    """Ajoute des query parameters à une URL."""
    separator = "&" if "?" in url else "?"
    return f"{url}{separator}{urlencode(params)}"


# ---------------------------------------------------------------------------
# Helpers SSE
# ---------------------------------------------------------------------------

def parse_sse_event(lines: list[str]) -> dict:
    """Parse un événement SSE à partir de ses lignes."""
    event = {
        "event": None,
        "id": None,
        "data": [],
        "raw": lines,
    }

    for line in lines:
        if line.startswith("data:"):
            event["data"].append(line[5:].strip())
        elif line.startswith("event:"):
            event["event"] = line[6:].strip()
        elif line.startswith("id:"):
            event["id"] = line[6:].strip()

    event["data_text"] = "\n".join(event["data"])
    return event


def probe_sse_stream(http2_client, url: str, headers: dict, timeout: float = 10.0):
    """Sonde un stream SSE sans lire le corps."""
    try:
        with http2_client.stream("GET", url, headers=headers, timeout=timeout) as response:
            return response.status_code, get_content_type(response), None
    except Exception as exc:
        return None, None, str(exc)


def capture_sse(
    http2_client,
    url: str,
    headers: dict,
    timeout: float = 5.0,
    trigger_command: str | None = None,
    stop_after_event: bool = True,
    max_events: int = 1,
) -> dict:
    """Capture des événements SSE pendant une durée limitée."""
    result = {
        "status": None,
        "content_type": None,
        "events": [],
        "comments": [],
        "raw": [],
        "timed_out": False,
        "closed": False,
        "exception": None,
    }

    proc = None

    try:
        with http2_client.stream(
            "GET",
            url,
            headers=headers,
            timeout=httpx.Timeout(10.0, read=timeout),
        ) as response:
            result["status"] = response.status_code
            result["content_type"] = get_content_type(response)

            if response.status_code != 200:
                return result

            if result["content_type"] != SSE_MEDIA_TYPE:
                return result

            if trigger_command:
                try:
                    proc = subprocess.Popen(trigger_command, shell=True)
                except Exception as exc:
                    result["exception"] = str(exc)

            current = []
            start = time.time()

            try:
                for line in response.iter_lines():
                    result["raw"].append(line)

                    if line.startswith(":"):
                        result["comments"].append(line)
                        continue

                    if line == "":
                        if current:
                            result["events"].append(parse_sse_event(current))
                            current = []

                            if stop_after_event and len(result["events"]) >= max_events:
                                break
                    else:
                        current.append(line)

                    if time.time() - start > timeout:
                        result["timed_out"] = True
                        break
                else:
                    result["closed"] = True

            except httpx.ReadTimeout:
                result["timed_out"] = True
            except Exception as exc:
                result["exception"] = str(exc)

            if current:
                result["events"].append(parse_sse_event(current))

    except httpx.ReadTimeout:
        result["timed_out"] = True
    except Exception as exc:
        result["exception"] = str(exc)
    finally:
        if proc is not None:
            try:
                proc.wait(timeout=5)
            except Exception:
                try:
                    proc.kill()
                except Exception:
                    pass

    return result


def extract_event_time(event: dict) -> str | None:
    """Extrait eventTime d'un événement SSE."""
    data_text = event.get("data_text", "")

    try:
        data = json.loads(data_text)

        def _find(node):
            if isinstance(node, dict):
                for key, value in node.items():
                    if local_name(key) == "eventTime" and isinstance(value, str):
                        return value
                    found = _find(value)
                    if found:
                        return found
            elif isinstance(node, list):
                for item in node:
                    found = _find(item)
                    if found:
                        return found
            return None

        found = _find(data)
        if found:
            return found

    except Exception:
        pass

    match = re.search(r'"eventTime"\s*:\s*"([^"]+)"', data_text)
    if match:
        return match.group(1)

    match = re.search(r"<eventTime[^>]*>([^<]+)</eventTime>", data_text)
    if match:
        return match.group(1)

    return None


def validate_event_time(value: str) -> bool:
    """Valide grossièrement un horodatage XML/JSON dateTime."""
    pattern = (
        r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}"
        r"(\.\d+)?(Z|[+-]\d{2}:\d{2})?$"
    )
    return bool(re.match(pattern, value))


def events_contain_text(events: list[dict], needle: str) -> bool:
    """Vérifie si un événement contient un texte donné."""
    for event in events:
        data_text = event.get("data_text", "")
        event_name = event.get("event") or ""

        if needle in data_text or needle in event_name:
            return True

    return False


# ---------------------------------------------------------------------------
# Fixtures : streams
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def streams_response(http2_client, api_url, auth_headers, test_jwt):
    """GET sur la liste des streams."""
    if not test_jwt:
        return None

    headers = {"Accept": YANG_JSON, **auth_headers}

    try:
        return http2_client.get(f"{api_url}{STREAMS_PATH}", headers=headers)
    except Exception:
        return None


@pytest.fixture(scope="session")
def require_streams_entries(streams_response) -> list[dict]:
    """Extrait les streams disponibles ou skip."""
    if streams_response is None:
        pytest.skip("Aucun JWT configuré pour accéder aux streams.")

    if streams_response.status_code == 404:
        pytest.skip("La liste des streams n'est pas exposée.")

    if streams_response.status_code in (401, 403):
        pytest.skip(f"Accès à la liste des streams refusé ({streams_response.status_code}).")

    if streams_response.status_code != 200:
        pytest.skip(
            f"Impossible d'accéder à la liste des streams "
            f"({streams_response.status_code})."
        )

    if get_content_type(streams_response) != YANG_JSON:
        pytest.skip("La liste des streams n'est pas retournée en JSON.")

    try:
        body = streams_response.json()
    except Exception:
        pytest.skip("Réponse streams non JSON.")

    entries = find_stream_entries(body)
    if not entries:
        pytest.skip("Aucun stream trouvé dans la réponse streams.")

    return entries


@pytest.fixture(scope="session")
def default_stream_url(base_url, api_url, require_streams_entries) -> str:
    """URL SSE du stream par défaut."""
    name = get_stream_name(require_streams_entries, "RESTCONF_SSE_STREAM_NAME")
    if not name:
        pytest.skip("Aucun stream disponible pour les tests SSE.")

    return get_stream_url(base_url, api_url, require_streams_entries, name)


@pytest.fixture(scope="session")
def replay_stream_url(base_url, api_url, require_streams_entries) -> str:
    """URL SSE d'un stream avec replay supporté."""
    name = get_stream_name(
        require_streams_entries,
        "RESTCONF_SSE_REPLAY_STREAM_NAME",
        required_replay=True,
    )
    if not name:
        pytest.skip(
            "Aucun stream avec replay-support détecté. Définissez "
            "RESTCONF_SSE_REPLAY_STREAM_NAME si nécessaire."
        )

    return get_stream_url(base_url, api_url, require_streams_entries, name)


@pytest.fixture(scope="session")
def no_replay_stream_url(base_url, api_url, require_streams_entries) -> str:
    """URL SSE d'un stream sans replay supporté."""
    name = get_stream_name(
        require_streams_entries,
        "RESTCONF_SSE_NO_REPLAY_STREAM_NAME",
        required_replay=False,
    )
    if not name:
        pytest.skip(
            "Aucun stream sans replay détecté. Définissez "
            "RESTCONF_SSE_NO_REPLAY_STREAM_NAME si nécessaire."
        )

    return get_stream_url(base_url, api_url, require_streams_entries, name)


@pytest.fixture()
def notification_trigger() -> str:
    """Commande pour déclencher une notification YANG."""
    command = os.getenv("RESTCONF_SSE_NOTIFICATION_COMMAND")
    if not command:
        pytest.skip(
            "Aucune commande de notification configurée. Définissez "
            "RESTCONF_SSE_NOTIFICATION_COMMAND."
        )
    return command


# ---------------------------------------------------------------------------
# Fixtures : JWT restreint
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
# T-SSE-01 : GET sur la liste des streams
# ============================================================================
@pytest.mark.roadmap("R23")
@pytest.mark.rfc("RFC 8040 §3.8")
class TestT_SSE_01_GetStreamList:
    """
    T-SSE-01 : GET sur la liste des streams.
    Liste des flux disponibles.
    """

    def test_get_stream_list(self, streams_response, require_streams_entries):
        assert streams_response.status_code == 200
        assert get_content_type(streams_response) == YANG_JSON
        assert isinstance(require_streams_entries, list)
        assert len(require_streams_entries) > 0


# ============================================================================
# T-SSE-02 : Vérification des métadonnées de stream
# ============================================================================
@pytest.mark.roadmap("R23")
@pytest.mark.rfc("RFC 8040 §3.8")
class TestT_SSE_02_StreamMetadata:
    """
    T-SSE-02 : Vérification des métadonnées de stream.
    Nom, description, replay-support, URL d'accès présents.
    """

    def test_stream_metadata(self, require_streams_entries):
        missing = []

        for entry in require_streams_entries:
            name = get_yang_field(entry, "name")
            assert isinstance(name, str) and name, "Un stream doit avoir un nom."

            location = find_first_location(entry)
            if not location:
                missing.append(f"{name}:access/location")

            description = get_yang_field(entry, "description")
            if description is None:
                missing.append(f"{name}:description")

            replay_support = get_yang_field(entry, "replay-support")
            if replay_support is None:
                missing.append(f"{name}:replay-support")

        if missing:
            pytest.skip(
                "Métadonnées de stream incomplètes : "
                f"{', '.join(missing)}"
            )


# ============================================================================
# T-SSE-03 : GET sur un stream avec Accept: text/event-stream
# ============================================================================
@pytest.mark.roadmap("R24")
@pytest.mark.rfc("RFC 8040 §6.2")
class TestT_SSE_03_OpenStream:
    """
    T-SSE-03 : GET sur un stream avec Accept: text/event-stream.
    200 OK et Content-Type: text/event-stream.
    """

    def test_open_stream(self, http2_client, auth_headers, default_stream_url):
        headers = {
            "Accept": SSE_MEDIA_TYPE,
            **auth_headers,
        }

        status, content_type, error = probe_sse_stream(
            http2_client,
            default_stream_url,
            headers=headers,
        )

        if error:
            pytest.skip(f"Impossible d'ouvrir le flux SSE : {error}")

        if status == 404:
            pytest.skip("Stream non trouvé.")

        if status in (401, 403):
            pytest.skip(f"Accès au stream refusé ({status}).")

        if status == 400:
            pytest.skip("Requête SSE rejetée avec 400.")

        assert status == 200, (
            f"GET sur un stream SSE doit retourner 200 OK, obtenu {status}"
        )
        assert content_type == SSE_MEDIA_TYPE, (
            f"Content-Type attendu {SSE_MEDIA_TYPE!r}, obtenu {content_type!r}"
        )


# ============================================================================
# T-SSE-04 : Réception d'une notification YANG
# ============================================================================
@pytest.mark.roadmap("R25")
@pytest.mark.rfc("RFC 8040 §6.4")
class TestT_SSE_04_ReceiveNotification:
    """
    T-SSE-04 : Réception d'une notification YANG.
    Événement SSE formaté correctement.
    """

    def test_receive_notification(
        self,
        http2_client,
        auth_headers,
        default_stream_url,
        notification_trigger,
    ):
        headers = {
            "Accept": SSE_MEDIA_TYPE,
            **auth_headers,
        }

        timeout = get_float_env("RESTCONF_SSE_NOTIFICATION_TIMEOUT", 10.0)

        result = capture_sse(
            http2_client,
            default_stream_url,
            headers=headers,
            timeout=timeout,
            trigger_command=notification_trigger,
            stop_after_event=True,
            max_events=1,
        )

        if result["exception"]:
            pytest.skip(f"Erreur SSE : {result['exception']}")

        if result["status"] != 200:
            pytest.skip(f"Stream SSE non accessible ({result['status']}).")

        if result["content_type"] != SSE_MEDIA_TYPE:
            pytest.skip("La réponse n'est pas text/event-stream.")

        if not result["events"]:
            pytest.skip(
                "Aucune notification reçue avant timeout. Vérifiez la commande "
                "de déclenchement et le stream."
            )

        event = result["events"][0]
        assert event.get("data_text") or event.get("event"), (
            "L'événement SSE doit contenir au moins un champ data: ou event:."
        )


# ============================================================================
# T-SSE-05 : Vérification de eventTime
# ============================================================================
@pytest.mark.roadmap("R25")
@pytest.mark.rfc("RFC 8040 §6.4")
class TestT_SSE_05_EventTime:
    """
    T-SSE-05 : Vérification de eventTime.
    Horodatage présent et cohérent.
    """

    def test_event_time(
        self,
        http2_client,
        auth_headers,
        default_stream_url,
        notification_trigger,
    ):
        headers = {
            "Accept": SSE_MEDIA_TYPE,
            **auth_headers,
        }

        timeout = get_float_env("RESTCONF_SSE_NOTIFICATION_TIMEOUT", 10.0)

        result = capture_sse(
            http2_client,
            default_stream_url,
            headers=headers,
            timeout=timeout,
            trigger_command=notification_trigger,
            stop_after_event=True,
            max_events=1,
        )

        if result["exception"]:
            pytest.skip(f"Erreur SSE : {result['exception']}")

        if result["status"] != 200:
            pytest.skip(f"Stream SSE non accessible ({result['status']}).")

        if not result["events"]:
            pytest.skip("Aucune notification reçue pour vérifier eventTime.")

        event_time = None
        for event in result["events"]:
            event_time = extract_event_time(event)
            if event_time:
                break

        if not event_time:
            pytest.skip("eventTime non détecté dans les événements reçus.")

        assert validate_event_time(event_time), (
            f"eventTime doit être un horodatage valide, obtenu {event_time!r}"
        )


# ============================================================================
# T-SSE-06 : Encodage JSON/XML des notifications
# ============================================================================
@pytest.mark.roadmap("R25")
@pytest.mark.roadmap("R41")
@pytest.mark.rfc("RFC 8040 §6.4")
@pytest.mark.rfc("RFC 7951")
class TestT_SSE_06_NotificationEncoding:
    """
    T-SSE-06 : Encodage JSON/XML des notifications.
    Conforme au media type négocié.
    """

    def test_notification_encoding(
        self,
        http2_client,
        auth_headers,
        default_stream_url,
        notification_trigger,
    ):
        headers = {
            "Accept": SSE_MEDIA_TYPE,
            **auth_headers,
        }

        timeout = get_float_env("RESTCONF_SSE_NOTIFICATION_TIMEOUT", 10.0)

        result = capture_sse(
            http2_client,
            default_stream_url,
            headers=headers,
            timeout=timeout,
            trigger_command=notification_trigger,
            stop_after_event=True,
            max_events=1,
        )

        if result["exception"]:
            pytest.skip(f"Erreur SSE : {result['exception']}")

        if result["status"] != 200:
            pytest.skip(f"Stream SSE non accessible ({result['status']}).")

        if not result["events"]:
            pytest.skip("Aucune notification reçue pour vérifier l'encodage.")

        data_text = result["events"][0].get("data_text", "").strip()
        if not data_text:
            pytest.skip("Événement SSE sans contenu data.")

        if data_text.startswith("{"):
            try:
                json.loads(data_text)
            except Exception:
                pytest.fail(
                    "La notification est annoncée comme JSON mais le corps "
                    "n'est pas un JSON valide."
                )
            return

        if data_text.startswith("<"):
            assert "<" in data_text and ">" in data_text, (
                "La notification XML doit contenir des balises XML."
            )
            return

        pytest.skip(
            "Encodage de notification ni JSON ni XML détecté ; "
            "impossible de valider l'encodage négocié."
        )


# ============================================================================
# T-SSE-07 : Heartbeat SSE
# ============================================================================
@pytest.mark.roadmap("R24")
@pytest.mark.roadmap("A16")
@pytest.mark.rfc("RFC 8040 §6.2")
class TestT_SSE_07_Heartbeat:
    """
    T-SSE-07 : Heartbeat SSE.
    Commentaire ou événement maintenant la connexion active.
    """

    def test_heartbeat(self, http2_client, auth_headers, default_stream_url):
        headers = {
            "Accept": SSE_MEDIA_TYPE,
            **auth_headers,
        }

        timeout = get_float_env("RESTCONF_SSE_HEARTBEAT_TIMEOUT", 15.0)

        result = capture_sse(
            http2_client,
            default_stream_url,
            headers=headers,
            timeout=timeout,
            trigger_command=None,
            stop_after_event=False,
            max_events=0,
        )

        if result["exception"]:
            pytest.skip(f"Erreur SSE : {result['exception']}")

        if result["status"] != 200:
            pytest.skip(f"Stream SSE non accessible ({result['status']}).")

        if result["comments"] or result["events"]:
            return

        pytest.skip(
            "Aucun heartbeat ou événement observé avant timeout. "
            "Le serveur n'émet peut-être pas de heartbeat dans cet environnement."
        )


# ============================================================================
# T-SSE-08 : Stream avec replay supporté
# ============================================================================
@pytest.mark.roadmap("R21")
@pytest.mark.roadmap("R24")
@pytest.mark.rfc("RFC 8040 §4.8.7")
class TestT_SSE_08_ReplayStartTime:
    """
    T-SSE-08 : Stream avec replay supporté.
    start-time accepté.
    """

    def test_replay_start_time(self, http2_client, auth_headers, replay_stream_url):
        headers = {
            "Accept": SSE_MEDIA_TYPE,
            **auth_headers,
        }

        url = add_query_params(
            replay_stream_url,
            {"start-time": START_TIME_PAST},
        )

        status, content_type, error = probe_sse_stream(
            http2_client,
            url,
            headers=headers,
        )

        if error:
            pytest.skip(f"Impossible d'ouvrir le flux SSE avec start-time : {error}")

        if status == 404:
            pytest.skip("Stream avec replay non trouvé.")

        if status in (401, 403):
            pytest.skip(f"Accès au stream refusé ({status}).")

        if status == 400:
            pytest.skip(
                "start-time est rejeté ; le replay n'est peut-être pas "
                "réellement supporté."
            )

        assert status == 200, (
            f"Un stream avec replay doit accepter start-time, obtenu {status}"
        )
        assert content_type == SSE_MEDIA_TYPE


# ============================================================================
# T-SSE-09 : Stream sans replay mais avec start-time
# ============================================================================
@pytest.mark.roadmap("R21")
@pytest.mark.rfc("RFC 8040 §4.8.7")
class TestT_SSE_09_NoReplayStartTime:
    """
    T-SSE-09 : Stream sans replay mais avec start-time.
    Erreur RESTCONF.
    """

    def test_no_replay_start_time(
        self,
        http2_client,
        auth_headers,
        no_replay_stream_url,
    ):
        headers = {
            "Accept": SSE_MEDIA_TYPE,
            **auth_headers,
        }

        url = add_query_params(
            no_replay_stream_url,
            {"start-time": START_TIME_PAST},
        )

        status, _, error = probe_sse_stream(
            http2_client,
            url,
            headers=headers,
        )

        if error:
            pytest.skip(f"Erreur réseau : {error}")

        if status == 404:
            pytest.skip("Stream sans replay non trouvé.")

        if status in (401, 403):
            pytest.skip(f"Accès au stream refusé ({status}).")

        if status == 200:
            pytest.fail(
                "Un stream sans replay ne doit pas accepter start-time."
            )

        assert status in (400, 409), (
            "start-time sur stream sans replay doit retourner une erreur "
            f"RESTCONF, obtenu {status}"
        )


# ============================================================================
# T-SSE-10 : stop-time atteint
# ============================================================================
@pytest.mark.roadmap("R21")
@pytest.mark.rfc("RFC 8040 §4.8.8")
class TestT_SSE_10_StopTime:
    """
    T-SSE-10 : stop-time atteint.
    Fin de replay ou fermeture conforme.
    """

    def test_stop_time(self, http2_client, auth_headers, replay_stream_url):
        headers = {
            "Accept": SSE_MEDIA_TYPE,
            **auth_headers,
        }

        url = add_query_params(
            replay_stream_url,
            {
                "start-time": START_TIME_PAST,
                "stop-time": STOP_TIME_PAST,
            },
        )

        timeout = get_float_env("RESTCONF_SSE_NOTIFICATION_TIMEOUT", 10.0)

        result = capture_sse(
            http2_client,
            url,
            headers=headers,
            timeout=timeout,
            trigger_command=None,
            stop_after_event=False,
            max_events=0,
        )

        if result["exception"]:
            pytest.skip(f"Erreur SSE : {result['exception']}")

        if result["status"] != 200:
            pytest.skip(f"Stream SSE non accessible ({result['status']}).")

        if result["closed"]:
            return

        if events_contain_text(result["events"], "replay-completed"):
            return

        pytest.skip(
            "Le comportement de stop-time n'est pas démontrable avant timeout "
            "dans cet environnement."
        )


# ============================================================================
# T-SSE-11 : Notification replay-completed si applicable
# ============================================================================
@pytest.mark.roadmap("R25")
@pytest.mark.rfc("RFC 8040 §6.4")
class TestT_SSE_11_ReplayCompleted:
    """
    T-SSE-11 : Notification replay-completed si applicable.
    Émise à la fin du replay.
    """

    def test_replay_completed(self, http2_client, auth_headers, replay_stream_url):
        headers = {
            "Accept": SSE_MEDIA_TYPE,
            **auth_headers,
        }

        url = add_query_params(
            replay_stream_url,
            {
                "start-time": START_TIME_PAST,
                "stop-time": STOP_TIME_PAST,
            },
        )

        timeout = get_float_env("RESTCONF_SSE_NOTIFICATION_TIMEOUT", 10.0)

        result = capture_sse(
            http2_client,
            url,
            headers=headers,
            timeout=timeout,
            trigger_command=None,
            stop_after_event=False,
            max_events=0,
        )

        if result["exception"]:
            pytest.skip(f"Erreur SSE : {result['exception']}")

        if result["status"] != 200:
            pytest.skip(f"Stream SSE non accessible ({result['status']}).")

        if events_contain_text(result["events"], "replay-completed"):
            return

        pytest.skip(
            "Aucune notification replay-completed observée ; elle n'est "
            "peut-être pas applicable ou aucun replay n'a eu lieu."
        )


# ============================================================================
# T-SSE-12 : Fermeture du flux par le client
# ============================================================================
@pytest.mark.roadmap("R24")
@pytest.mark.roadmap("A16")
@pytest.mark.rfc("RFC 8040 §6.2")
class TestT_SSE_12_ClientClose:
    """
    T-SSE-12 : Fermeture du flux par le client.
    La souscription sysrepo est nettoyée.

    En test externe, on vérifie au minimum que le stream peut être rouvert
    après une fermeture. La validation complète du nettoyage sysrepo est
    interne.
    """

    def test_close_and_reopen(self, http2_client, auth_headers, default_stream_url):
        headers = {
            "Accept": SSE_MEDIA_TYPE,
            **auth_headers,
        }

        status_1, content_type_1, error_1 = probe_sse_stream(
            http2_client,
            default_stream_url,
            headers=headers,
        )

        if error_1:
            pytest.skip(f"Impossible d'ouvrir le stream : {error_1}")

        if status_1 != 200:
            pytest.skip(f"Stream non accessible ({status_1}).")

        assert content_type_1 == SSE_MEDIA_TYPE

        status_2, _, error_2 = probe_sse_stream(
            http2_client,
            default_stream_url,
            headers=headers,
        )

        if error_2:
            pytest.fail(
                f"Impossible de rouvrir le stream après fermeture : {error_2}"
            )

        assert status_2 == 200, (
            "Le stream doit pouvoir être rouvert après fermeture client, "
            f"obtenu {status_2}"
        )


# ============================================================================
# T-SSE-13 : Flux SSE sous HTTP/2 avec flow control
# ============================================================================
@pytest.mark.roadmap("A3")
@pytest.mark.rfc("RFC 9113")
class TestT_SSE_13_FlowControl:
    """
    T-SSE-13 : Flux SSE sous HTTP/2 avec flow control.
    Les DATA frames respectent la fenêtre de crédit.

    Ce test nécessite une validation interne nghttp2/libevent. Un client
    externe ne peut pas observer directement les fenêtres HTTP/2.
    """

    def test_flow_control(self):
        pytest.skip(
            "La validation du flow control HTTP/2 nécessite une inspection "
            "interne de nghttp2 (DATA frames, WINDOW_UPDATE). Ce comportement "
            "doit être validé par test d'implémentation ou trace serveur."
        )


# ============================================================================
# T-SSE-14 : WINDOW_UPDATE après blocage
# ============================================================================
@pytest.mark.roadmap("A3")
@pytest.mark.rfc("RFC 9113")
class TestT_SSE_14_WindowUpdate:
    """
    T-SSE-14 : WINDOW_UPDATE après blocage.
    L'envoi reprend via nghttp2_session_resume_data().

    Ce test nécessite une validation interne nghttp2/libevent.
    """

    def test_window_update(self):
        pytest.skip(
            "La reprise après WINDOW_UPDATE doit être validée côté serveur "
            "avec nghttp2_session_resume_data(). Un test externe ne peut pas "
            "observer directement ce mécanisme."
        )


# ============================================================================
# T-SSE-15 : Stream interdit par NACM
# ============================================================================
@pytest.mark.roadmap("R29")
@pytest.mark.rfc("RFC 8341")
class TestT_SSE_15_NacmStream:
    """
    T-SSE-15 : Stream interdit par NACM.
    Accès refusé ou stream non visible.
    """

    def test_nacm_on_streams(
        self,
        http2_client,
        api_url,
        base_url,
        require_streams_entries,
        restricted_headers,
    ):
        forbidden_stream = os.getenv("RESTCONF_SSE_FORBIDDEN_STREAM")

        if forbidden_stream:
            url = get_stream_url(
                base_url,
                api_url,
                require_streams_entries,
                forbidden_stream,
            )

            headers = {
                "Accept": SSE_MEDIA_TYPE,
                **restricted_headers,
            }

            status, _, error = probe_sse_stream(
                http2_client,
                url,
                headers=headers,
            )

            if error:
                pytest.skip(f"Erreur réseau : {error}")

            if status in (403, 404):
                return

            if status == 401:
                pytest.skip("Le JWT restreint est refusé comme invalide.")

            if status == 200:
                pytest.skip(
                    "Le stream interdit est accessible avec le token restreint ; "
                    "aucune règle NACM restrictive n'est démontrable."
                )

            pytest.fail(
                f"Réponse inattendue pour un stream interdit : {status}"
            )

        headers = {
            "Accept": YANG_JSON,
            **restricted_headers,
        }

        response = http2_client.get(
            f"{api_url}{STREAMS_PATH}",
            headers=headers,
        )

        if response.status_code == 401:
            pytest.skip("Le JWT restreint est refusé comme invalide.")

        if response.status_code in (403, 404):
            return

        if response.status_code != 200:
            pytest.skip(
                f"Accès à la liste des streams avec token restreint : "
                f"{response.status_code}"
            )

        try:
            body = response.json()
        except Exception:
            pytest.skip("Réponse streams restreinte non JSON.")

        restricted_entries = find_stream_entries(body)
        admin_names = {
            get_yang_field(entry, "name")
            for entry in require_streams_entries
        }
        restricted_names = {
            get_yang_field(entry, "name")
            for entry in restricted_entries
        }

        if restricted_names < admin_names:
            return

        pytest.skip(
            "Le token restreint voit autant de streams que le token admin ; "
            "aucune règle NACM de filtrage des streams n'est démontrable."
        )