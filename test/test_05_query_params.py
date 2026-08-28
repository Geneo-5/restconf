# SPDX-License-Identifier: LGPL-3.0-only
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN
"""Tests de conformité RESTCONF - Section 5 : Paramètres de requête GET.

Couvre les items T-QUERY-01 à T-QUERY-15 de la ROADMAP.md, ainsi que des
vérifications de format basées sur restconf-test.yang et restconf-test.json.

RFC liées : RFC 8040 §4.8, RFC 6243.
"""

from __future__ import annotations

from typing import Any, Callable, Optional

import pytest

# ---------------------------------------------------------------------------
# Constantes normatives et chemins
# ---------------------------------------------------------------------------

YANG_JSON = "application/yang-data+json"
EVENT_STREAM = "text/event-stream"

MOD = "restconf-test"

BASIC_DATA = "/data/restconf-test:basic-data"
SYSTEM = "/data/restconf-test:system"
SYSTEM_STATE = "/data/restconf-test:system/state"
INTERFACES = "/data/restconf-test:interfaces"
ACCESS_CONTROL = "/data/restconf-test:access-control"
ADVANCED_TYPES = "/data/restconf-test:advanced-types"

# Chemin de stream générique (à ajuster si l'implémentation expose les
# streams ailleurs).
STREAM_PATH = "/streams/stream/NETCONF"

# ---------------------------------------------------------------------------
# Données attendues depuis restconf-test.json
# ---------------------------------------------------------------------------

EXPECTED_BASIC_CONFIG = {
    "device-id": "dut-01",
    "timeout": 30,
    "enabled": True,
}

EXPECTED_SYSTEM_NAME = "restconf-dut"

EXPECTED_INTERFACES_BY_NAME = {
    "eth0": {
        "description": "Uplink interface",
        "enabled": True,
        "mtu": 1500,
    },
    "eth1": {
        "description": "Management interface",
        "enabled": False,
        "mtu": 9000,
    },
    "lo0": {
        "description": "Loopback",
        "enabled": True,
        "mtu": 1500,
    },
}

EXPECTED_VLANS_BY_ID = {
    100: {"name": "mgmt"},
    200: {"name": "data"},
}

EXPECTED_ORDERED_QUEUE_ITEMS = {
    1: "first",
    2: "second",
    3: "third",
}

EXPECTED_ACCESS_CONTROL = {
    "allowed-ips": [
        "192.0.2.1",
        "198.51.100.7",
        "203.0.113.9",
    ],
    "allowed-macs": [
        "00:11:22:33:44:55",
        "aa:bb:cc:dd:ee:ff",
    ],
    "tags": [
        "production",
        "edge",
        "restconf",
    ],
}

EXPECTED_ADVANCED_TYPES = {
    "mixed-value": "hello",
    "feature-flags": {"logging", "monitoring"},
    "precision-value": "3.1416",
    "certificate": "AQIDBA==",
    "sensor-type-ref": "restconf-test:temperature-sensor",
    "default-interface": "eth0",
}

# Tags d'erreur acceptés pour les paramètres de requête invalides ou non
# supportés. Le tag attendu par RFC 8040 est généralement invalid-value,
# mais certaines implémentations utilisent operation-not-supported pour une
# capacité optionnelle non implémentée.
QUERY_ERROR_TAGS = {
    "invalid-value",
    "operation-not-supported",
    "bad-attribute",
    "unknown-attribute",
    "missing-attribute",
}

# ---------------------------------------------------------------------------
# Helpers HTTP / RESTCONF
# ---------------------------------------------------------------------------


def get_content_type(response) -> str:
    """Retourne le Content-Type sans les paramètres comme charset."""
    return response.headers.get("content-type", "").split(";")[0].strip().lower()


def response_json(response):
    """Décode le JSON avec un message d'échec lisible."""
    try:
        return response.json()
    except Exception as exc:
        pytest.fail(
            f"Réponse JSON invalide "
            f"(status={response.status_code}, content-type={get_content_type(response)!r}) : "
            f"{exc}\nCorps (tronqué) : {response.text[:2000]!r}"
        )


def assert_yang_content_type(response, expected: str = YANG_JSON) -> None:
    ctype = get_content_type(response)
    assert ctype == expected, f"Content-Type inattendu : {ctype!r} != {expected!r}"


def get_top(body, top: str) -> dict:
    """Retourne le conteneur JSON de premier niveau attendu."""
    assert isinstance(body, dict), f"Le corps JSON doit être un objet : {body!r}"
    assert top in body, f"Le nœud racine {top!r} est absent : {sorted(body.keys())}"
    data = body[top]
    assert isinstance(data, dict), f"{top!r} doit être un objet JSON : {data!r}"
    return data


def visible_keys(data: dict) -> set[str]:
    """Retourne les clés JSON visibles, sans les métadonnées '@...'."""
    return {k for k in data.keys() if isinstance(k, str) and not k.startswith("@")}


def contains_key(obj, key: str) -> bool:
    """Recherche récursive d'une clé JSON."""
    if isinstance(obj, dict):
        if key in obj:
            return True
        return any(contains_key(v, key) for v in obj.values())

    if isinstance(obj, list):
        return any(contains_key(item, key) for item in obj)

    return False


def contains_value(obj, value) -> bool:
    """Recherche récursive d'une valeur JSON."""
    if isinstance(obj, dict):
        return any(v == value or contains_value(v, value) for v in obj.values())

    if isinstance(obj, list):
        return any(item == value or contains_value(item, value) for item in obj)

    return obj == value


def assert_invalid_value_error(response, expected_statuses=(400, 404)):
    """
    Valide une erreur RESTCONF avec error-tag=invalid-value.

    Important : ici, les erreurs 404 sont attendues avec 'invalid-value',
    pas avec 'data-missing'.
    """
    assert response.status_code in expected_statuses, (
        f"Status inattendu : {response.status_code}, "
        f"attendu dans {expected_statuses}\nCorps : {response.text[:2000]!r}"
    )

    assert_yang_content_type(response, YANG_JSON)

    body = response_json(response)
    assert isinstance(body, dict), f"Corps d'erreur invalide : {body!r}"
    assert "ietf-restconf:errors" in body, f"Erreurs RESTCONF absentes : {body!r}"

    errors_obj = body["ietf-restconf:errors"]
    assert isinstance(errors_obj, dict), f"ietf-restconf:errors invalide : {errors_obj!r}"

    errors = errors_obj.get("error")
    assert isinstance(errors, list) and errors, f"Liste 'error' absente ou vide : {errors_obj!r}"

    tags = {
        err.get("error-tag")
        for err in errors
        if isinstance(err, dict)
    }

    assert "invalid-value" in tags, (
        f"error-tag=invalid-value attendu, tags reçus : {sorted(tags)}"
    )


def assert_ok_or_invalid_value_unsupported(response, validator=None) -> bool:
    """
    Helper pour capacités optionnelles.

    - 200 : la capacité est supportée, on valide le corps.
    - 400/404 : capacité non supportée ou valeur invalide, mais l'erreur
      doit être 'invalid-value'.
    """
    if response.status_code == 200:
        assert_yang_content_type(response, YANG_JSON)
        body = response_json(response)
        if validator is not None:
            validator(body)
        return True

    if response.status_code in (400, 404):
        assert_invalid_value_error(response, expected_statuses=(400, 404))
        return False

    pytest.fail(
        f"Status inattendu : {response.status_code}\nCorps : {response.text[:2000]!r}"
    )


# ---------------------------------------------------------------------------
# Fixtures locales
# ---------------------------------------------------------------------------

_RT_PROBE_PATHS = (
    BASIC_DATA,
    SYSTEM,
    INTERFACES,
)


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

    if any(code == 200 for code in statuses.values()):
        return rt_probe

    if all(code == 404 for code in statuses.values()):
        pytest.skip(
            "Module YANG 'restconf-test' non installé ou données non chargées "
            f"({statuses})"
        )

    if all(code in (401, 403) for code in statuses.values()):
        pytest.skip(f"Accès au datastore restconf-test refusé ({statuses})")

    pytest.skip(f"Datastore restconf-test inaccessible ({statuses})")


@pytest.fixture(scope="session")
def rt_oper_uptime_available(http2_client, api_url, auth_headers, test_jwt) -> bool:
    """Indique si les données opérationnelles uptime sont effectivement fournies."""
    if not test_jwt:
        return False

    headers = {"Accept": YANG_JSON, **auth_headers}
    try:
        resp = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=headers,
            params={"content": "nonconfig"},
        )
    except Exception:
        return False

    if resp.status_code != 200:
        return False

    try:
        body = resp.json()
    except Exception:
        return False

    data = body.get(f"{MOD}:basic-data", {})
    return isinstance(data, dict) and "uptime" in data

@pytest.fixture(scope="session")
def fields_supported(http2_client, api_url, auth_headers, test_jwt) -> bool:
    """
    Détecte si le serveur supporte le query parameter 'fields'.
    """
    if not test_jwt:
        return False

    headers = {"Accept": YANG_JSON, **auth_headers}

    try:
        response = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=headers,
            params={"fields": "device-id"},
        )
    except Exception:
        return False

    if response.status_code != 200:
        return False

    try:
        body = response.json()
    except Exception:
        return False

    data = body.get(f"{MOD}:basic-data", {})
    return (
        isinstance(data, dict)
        and data.get("device-id") == EXPECTED_BASIC_CONFIG["device-id"]
    )


@pytest.fixture()
def require_fields(fields_supported):
    """Skip si 'fields' n'est pas supporté."""
    if not fields_supported:
        pytest.skip("Le paramètre de requête 'fields' n'est pas supporté")
    return fields_supported

@pytest.fixture(scope="session")
def filter_syntax(http2_client, api_url, auth_headers, test_jwt):
    """
    Détecte une syntaxe de 'filter' supportée.

    On essaie plusieurs syntaxes possibles, car la forme exacte peut dépendre
    de l'implémentation. Si aucune ne fonctionne, les tests filter sont
    skippés.
    """
    if not test_jwt:
        return {}

    headers = {"Accept": YANG_JSON, **auth_headers}
    found = {}

    # ------------------------------------------------------------------
    # filter sur une feuille simple : basic-data/device-id
    # ------------------------------------------------------------------
    basic_candidates = (
        f"/{MOD}:basic-data/device-id",
        "device-id",
        f"{MOD}:basic-data/device-id",
        "basic-data/device-id",
    )

    for value in basic_candidates:
        try:
            response = http2_client.get(
                f"{api_url}{BASIC_DATA}",
                headers=headers,
                params={"filter": value},
            )
        except Exception:
            continue

        if response.status_code != 200:
            continue

        try:
            body = response.json()
        except Exception:
            continue

        # Le filtre doit retourner device-id, mais pas les autres feuilles.
        if (
            contains_value(body, EXPECTED_BASIC_CONFIG["device-id"])
            and not contains_key(body, "timeout")
            and not contains_key(body, "enabled")
            and not contains_key(body, "uptime")
        ):
            found["basic_device_id"] = value
            break

    # ------------------------------------------------------------------
    # filter sur une entrée de liste : interfaces/interface[name='eth0']
    # ------------------------------------------------------------------
    interface_candidates = (
        f"/{MOD}:interfaces/interface[name='eth0']",
        "interface[name='eth0']",
        f"{MOD}:interfaces/interface[name='eth0']",
        f"/{MOD}:interfaces/interface[name=\"eth0\"]",
    )

    for value in interface_candidates:
        try:
            response = http2_client.get(
                f"{api_url}{INTERFACES}",
                headers=headers,
                params={"filter": value},
            )
        except Exception:
            continue

        if response.status_code != 200:
            continue

        try:
            body = response.json()
        except Exception:
            continue

        # Doit contenir eth0, mais pas eth1/lo0 ni le MTU 9000 de eth1.
        if (
            contains_value(body, "eth0")
            and not contains_value(body, "eth1")
            and not contains_value(body, "lo0")
            and not contains_value(body, 9000)
        ):
            found["interface_eth0"] = value
            break

    return found


@pytest.fixture()
def require_filter(filter_syntax):
    """Skip si aucune syntaxe 'filter' n'a été reconnue."""
    if not filter_syntax:
        pytest.skip(
            "Aucune syntaxe de query parameter 'filter' reconnue par le serveur"
        )
    return filter_syntax

# ===========================================================================
# T-QUERY-01 : GET avec content=config
# ===========================================================================


@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8.1")
class TestT_QUERY_01_ContentConfig:
    """T-QUERY-01 : GET avec content=config.

    Seules les données de configuration (config true) doivent être retournées.
    """

    def test_content_config_basic_data(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=headers,
            params={"content": "config"},
        )

        assert response.status_code == 200
        assert_yang_content_type(response)

        body = response_json(response)
        data = get_top(body, f"{MOD}:basic-data")

        # basic-data contient :
        # - config true  : device-id, timeout, enabled
        # - config false : uptime
        assert set(data.keys()) <= {"device-id", "timeout", "enabled"}, (
            f"content=config ne doit retourner que les feuilles config true : {data}"
        )
        assert "uptime" not in data, "uptime est config false et doit être masqué"

        if "device-id" in data:
            assert data["device-id"] == EXPECTED_BASIC_CONFIG["device-id"]

        if "timeout" in data:
            assert data["timeout"] == EXPECTED_BASIC_CONFIG["timeout"]

        if "enabled" in data:
            assert data["enabled"] == EXPECTED_BASIC_CONFIG["enabled"]

    def test_content_config_system(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{SYSTEM}",
            headers=headers,
            params={"content": "config"},
        )

        assert response.status_code == 200
        assert_yang_content_type(response)

        body = response_json(response)
        system = get_top(body, f"{MOD}:system")

        # system/state est config false.
        assert "state" not in system, (
            f"content=config doit masquer system/state : {system}"
        )

        # system/config est config true et contient system-name.
        assert "config" in system, f"system/config attendu : {system}"
        config = system["config"]
        assert isinstance(config, dict)
        assert config.get("system-name") == EXPECTED_SYSTEM_NAME

    def test_content_config_interfaces(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{INTERFACES}",
            headers=headers,
            params={"content": "config"},
        )

        assert response.status_code == 200
        assert_yang_content_type(response)

        body = response_json(response)
        interfaces = get_top(body, f"{MOD}:interfaces")

        # Le container interfaces ne contient que des données config true dans
        # le modèle de test. On vérifie donc la présence des listes attendues.
        assert isinstance(interfaces.get("interface"), list), (
            f"interfaces/interface doit être une liste JSON : {interfaces}"
        )

        by_name = {
            entry.get("name"): entry
            for entry in interfaces["interface"]
            if isinstance(entry, dict)
        }

        for name, expected in EXPECTED_INTERFACES_BY_NAME.items():
            assert name in by_name, f"Interface {name!r} manquante : {interfaces}"
            entry = by_name[name]

            for leaf, value in expected.items():
                assert leaf in entry, (
                    f"Feuille {leaf!r} manquante dans l'interface {name!r} : {entry}"
                )
                assert entry[leaf] == value, (
                    f"Valeur inattendue pour {name}.{leaf} : "
                    f"{entry[leaf]!r} != {value!r}"
                )


# ===========================================================================
# T-QUERY-02 : GET avec content=nonconfig
# ===========================================================================


@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8.1")
class TestT_QUERY_02_ContentNonconfig:
    """T-QUERY-02 : GET avec content=nonconfig.

    Seules les données d'état (config false) doivent être retournées.
    """

    def test_content_nonconfig_basic_data(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_rt,
        rt_oper_uptime_available,
    ):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=headers,
            params={"content": "nonconfig"},
        )

        # 200 si le plugin opérationnel fournit uptime.
        # 404 si aucune donnée nonconfig n'est disponible.
        assert response.status_code in (200, 404)

        if response.status_code != 200:
            return

        assert_yang_content_type(response)

        body = response_json(response)
        data = body.get(f"{MOD}:basic-data")
        assert data is not None, (
            f"Réponse 200 sans {MOD}:basic-data : {body}"
        )
        assert isinstance(data, dict)

        # Les feuilles config true doivent être masquées.
        assert "device-id" not in data
        assert "timeout" not in data
        assert "enabled" not in data

        # Seule uptime est config false dans basic-data.
        assert set(data.keys()) <= {"uptime"}, (
            f"content=nonconfig doit limiter basic-data à uptime : {data}"
        )

        if rt_oper_uptime_available:
            assert "uptime" in data, (
                "uptime est censé être disponible mais est absent de la réponse"
            )

        if "uptime" in data:
            assert isinstance(data["uptime"], int), (
                f"uptime doit être un entier JSON : {data['uptime']!r}"
            )
            assert data["uptime"] >= 0

    def test_content_nonconfig_system(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{SYSTEM}",
            headers=headers,
            params={"content": "nonconfig"},
        )

        # 200 si le plugin opérationnel fournit system/state.
        # 404 si aucune donnée opérationnelle n'est disponible.
        assert response.status_code in (200, 404)

        if response.status_code != 200:
            return

        assert_yang_content_type(response)

        body = response_json(response)
        system = body.get(f"{MOD}:system")
        assert system is not None, f"Réponse 200 sans {MOD}:system : {body}"
        assert isinstance(system, dict)

        # system/config est config true et doit être masqué.
        assert "config" not in system, (
            f"content=nonconfig doit masquer system/config : {system}"
        )

        # system/state est config false.
        assert "state" in system, f"system/state attendu : {system}"
        state = system["state"]
        assert isinstance(state, dict)

        assert "system-status" in state, (
            f"system/state/system-status attendu : {state}"
        )
        assert isinstance(state["system-status"], str)

        # extended-status n'existe que si la feature advanced-monitoring est
        # activée. Sa présence est donc optionnelle ici.
        if "extended-status" in state:
            assert isinstance(state["extended-status"], str)


# ===========================================================================
# T-QUERY-03 : GET avec content=all
# ===========================================================================


@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8.1")
class TestT_QUERY_03_ContentAll:
    """T-QUERY-03 : GET avec content=all.

    Les données de configuration et d'état doivent être retournées.
    """

    def test_content_all_basic_data(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_rt,
        rt_oper_uptime_available,
    ):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=headers,
            params={"content": "all"},
        )

        assert response.status_code == 200
        assert_yang_content_type(response)

        body = response_json(response)
        data = get_top(body, f"{MOD}:basic-data")

        assert data.get("device-id") == EXPECTED_BASIC_CONFIG["device-id"]

        if "timeout" in data:
            assert data["timeout"] == EXPECTED_BASIC_CONFIG["timeout"]

        if "enabled" in data:
            assert data["enabled"] == EXPECTED_BASIC_CONFIG["enabled"]

        if rt_oper_uptime_available:
            assert "uptime" in data, (
                "uptime est disponible côté opérationnel mais absent de content=all"
            )

        if "uptime" in data:
            assert isinstance(data["uptime"], int)
            assert data["uptime"] >= 0

    def test_content_all_interfaces(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{INTERFACES}",
            headers=headers,
            params={"content": "all"},
        )

        assert response.status_code == 200
        assert_yang_content_type(response)

        body = response_json(response)
        interfaces = get_top(body, f"{MOD}:interfaces")

        # interface[]
        assert isinstance(interfaces.get("interface"), list)
        by_name = {
            entry.get("name"): entry
            for entry in interfaces["interface"]
            if isinstance(entry, dict)
        }

        for name, expected in EXPECTED_INTERFACES_BY_NAME.items():
            assert name in by_name, f"Interface {name!r} manquante"
            entry = by_name[name]

            for leaf, value in expected.items():
                assert leaf in entry, f"Feuille {leaf!r} manquante dans {name!r}"
                assert entry[leaf] == value, (
                    f"{name}.{leaf} : {entry[leaf]!r} != {value!r}"
                )

        # vlan[]
        assert isinstance(interfaces.get("vlan"), list)
        vlan_by_id = {
            entry.get("vlan-id"): entry
            for entry in interfaces["vlan"]
            if isinstance(entry, dict)
        }

        for vlan_id, expected in EXPECTED_VLANS_BY_ID.items():
            assert vlan_id in vlan_by_id, f"VLAN {vlan_id} manquant"
            assert vlan_by_id[vlan_id].get("name") == expected["name"]

        # ordered-queue[] est ordered-by user : les positions doivent être des
        # entiers et les items des chaînes. On vérifie aussi les valeurs
        # initiales de restconf-test.json.
        assert isinstance(interfaces.get("ordered-queue"), list)
        queue_by_pos = {}
        for entry in interfaces["ordered-queue"]:
            assert isinstance(entry, dict)
            assert isinstance(entry.get("position"), int)
            assert isinstance(entry.get("item"), str)
            queue_by_pos[entry["position"]] = entry["item"]

        for position, item in EXPECTED_ORDERED_QUEUE_ITEMS.items():
            assert queue_by_pos.get(position) == item, (
                f"ordered-queue position {position} : "
                f"{queue_by_pos.get(position)!r} != {item!r}"
            )

        # advanced-features : la contrainte when doit être satisfaite dans les
        # données initiales (enable-advanced=true).
        adv = interfaces.get("advanced-features")
        if adv is not None:
            assert isinstance(adv, dict)
            assert adv.get("enable-advanced") is True

            adv_ifaces = adv.get("advanced-interface", [])
            assert isinstance(adv_ifaces, list)
            assert any(
                isinstance(e, dict) and e.get("name") == "adv0"
                for e in adv_ifaces
            ), f"advanced-interface adv0 attendu : {adv_ifaces}"

    def test_content_all_access_control(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{ACCESS_CONTROL}",
            headers=headers,
            params={"content": "all"},
        )

        assert response.status_code == 200
        assert_yang_content_type(response)

        body = response_json(response)
        data = get_top(body, f"{MOD}:access-control")

        # leaf-list => tableaux JSON.
        for leaf_list in ("allowed-ips", "allowed-macs", "tags"):
            assert isinstance(data.get(leaf_list), list), (
                f"{leaf_list} doit être une liste JSON : {data}"
            )

        assert set(EXPECTED_ACCESS_CONTROL["allowed-ips"]).issubset(
            set(data.get("allowed-ips", []))
        )
        assert set(EXPECTED_ACCESS_CONTROL["allowed-macs"]).issubset(
            set(data.get("allowed-macs", []))
        )
        assert set(EXPECTED_ACCESS_CONTROL["tags"]).issubset(
            set(data.get("tags", []))
        )

    def test_content_all_advanced_types(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{ADVANCED_TYPES}",
            headers=headers,
            params={"content": "all"},
        )

        assert response.status_code == 200
        assert_yang_content_type(response)

        body = response_json(response)
        data = get_top(body, f"{MOD}:advanced-types")

        if "mixed-value" in data:
            # Valeur initiale "hello" : union, donc encodée en string JSON ici.
            assert data["mixed-value"] == EXPECTED_ADVANCED_TYPES["mixed-value"]

        if "feature-flags" in data:
            # bits : chaîne contenant les bits séparés par des espaces.
            assert isinstance(data["feature-flags"], str)
            flags = set(data["feature-flags"].split())
            assert flags == EXPECTED_ADVANCED_TYPES["feature-flags"], (
                f"feature-flags inattendu : {data['feature-flags']!r}"
            )

        if "precision-value" in data:
            # decimal64 : encodage JSON recommandé sous forme de chaîne pour
            # préserver la précision.
            assert isinstance(data["precision-value"], str)
            assert data["precision-value"] == EXPECTED_ADVANCED_TYPES["precision-value"]

        if "certificate" in data:
            # binary : base64.
            assert isinstance(data["certificate"], str)
            assert data["certificate"] == EXPECTED_ADVANCED_TYPES["certificate"]

        if "sensor-type-ref" in data:
            # identityref : valeur canonique avec préfixe.
            assert data["sensor-type-ref"] == EXPECTED_ADVANCED_TYPES["sensor-type-ref"], (f"{body}")

        if "default-interface" in data:
            # leafref : valeur de la feuille ciblée.
            assert data["default-interface"] == EXPECTED_ADVANCED_TYPES["default-interface"]


# ===========================================================================
# T-QUERY-04 : GET avec depth=1
# ===========================================================================


@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8.2")
class TestT_QUERY_04_Depth1:
    """T-QUERY-04 : GET avec depth=1.

    Seuls les enfants directs de la ressource cible doivent être inclus.
    """

    def test_depth_1_system(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{SYSTEM}",
            headers=headers,
            params={"depth": "1"},
        )

        def validate(body: Any) -> None:
            system = get_top(body, f"{MOD}:system")

            # depth=1 ne doit pas exposer les feuilles situées sous les
            # containers config/state.
            assert not contains_key(system, "system-name"), (
                f"depth=1 doit masquer system/config/system-name : {system}"
            )
            assert not contains_key(system, "system-status"), (
                f"depth=1 doit masquer system/state/system-status : {system}"
            )
            assert not contains_key(system, "extended-status"), (
                f"depth=1 doit masquer system/state/extended-status : {system}"
            )

        assert_ok_or_invalid_value_unsupported(response, validate)

    def test_depth_1_basic_data(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=headers,
            params={"depth": "1"},
        )

        def validate(body: Any) -> None:
            data = get_top(body, f"{MOD}:basic-data")

            # basic-data ne contient que des feuilles directes.
            assert set(data.keys()) <= {"device-id", "timeout", "enabled", "uptime"}, (
                f"depth=1 sur basic-data ne doit pas créer de sous-arbre : {data}"
            )

            if "device-id" in data:
                assert data["device-id"] == EXPECTED_BASIC_CONFIG["device-id"]

            if "timeout" in data:
                assert data["timeout"] == EXPECTED_BASIC_CONFIG["timeout"]

            if "enabled" in data:
                assert data["enabled"] == EXPECTED_BASIC_CONFIG["enabled"]

            if "uptime" in data:
                assert isinstance(data["uptime"], int)
                assert data["uptime"] >= 0

        assert_ok_or_invalid_value_unsupported(response, validate)


# ===========================================================================
# T-QUERY-05 : GET avec depth=unbounded
# ===========================================================================


@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8.2")
class TestT_QUERY_05_DepthUnbounded:
    """T-QUERY-05 : GET avec depth=unbounded.

    L'arbre complet doit être retourné.
    """

    def test_depth_unbounded_system(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{SYSTEM}",
            headers=headers,
            params={"depth": "unbounded"},
        )

        def validate(body: Any) -> None:
            system = get_top(body, f"{MOD}:system")

            # Si config est présent, ses feuilles doivent l'être aussi.
            if "config" in system:
                config = system["config"]
                assert isinstance(config, dict)
                assert config.get("system-name") == EXPECTED_SYSTEM_NAME, (
                    f"system/config/system-name attendu : {config}"
                )

            # Si state est présent, ses feuilles doivent l'être aussi.
            if "state" in system:
                state = system["state"]
                assert isinstance(state, dict)
                assert "system-status" in state, (
                    f"system/state/system-status attendu : {state}"
                )

        assert_ok_or_invalid_value_unsupported(response, validate)

    def test_depth_unbounded_interfaces(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{INTERFACES}",
            headers=headers,
            params={"depth": "unbounded"},
        )

        def validate(body: Any) -> None:
            interfaces = get_top(body, f"{MOD}:interfaces")

            if interfaces.get("interface"):
                assert any(
                    isinstance(entry, dict) and "mtu" in entry
                    for entry in interfaces["interface"]
                ), (
                    "depth=unbounded doit inclure les feuilles profondes "
                    f"(mtu) des interfaces : {interfaces}"
                )

        assert_ok_or_invalid_value_unsupported(response, validate)


# ===========================================================================
# T-QUERY-06 : GET avec fields valide
# ===========================================================================


@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8.3")
class TestT_QUERY_06_FieldsValid:
    """
    T-QUERY-06 : GET avec fields valide.

    On vérifie que seuls les champs demandés sont retournés.
    """

    def test_fields_top_leaf(self, http2_client, api_url, auth_headers, require_rt, require_fields):
        headers = {"Accept": YANG_JSON, **auth_headers}

        response = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=headers,
            params={"fields": "device-id"},
        )

        assert response.status_code == 200
        assert_yang_content_type(response)

        body = response_json(response)
        data = get_top(body, f"{MOD}:basic-data")

        assert visible_keys(data) == {"device-id"}, (
            f"fields=device-id doit retourner uniquement device-id : {data}"
        )
        assert data["device-id"] == EXPECTED_BASIC_CONFIG["device-id"]

    def test_fields_multiple_top_leaves(self, http2_client, api_url, auth_headers, require_rt, require_fields):
        headers = {"Accept": YANG_JSON, **auth_headers}

        response = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=headers,
            params={"fields": "device-id;timeout"},
        )

        assert response.status_code == 200
        assert_yang_content_type(response)

        body = response_json(response)
        data = get_top(body, f"{MOD}:basic-data")

        keys = visible_keys(data)

        assert keys == {"device-id", "timeout"}, (
            f"fields=device-id;timeout doit retourner uniquement ces feuilles : {data}"
        )
        assert data["device-id"] == EXPECTED_BASIC_CONFIG["device-id"]
        assert data["timeout"] == EXPECTED_BASIC_CONFIG["timeout"]

    def test_fields_nested_container_leaf(self, http2_client, api_url, auth_headers, require_rt, require_fields):
        headers = {"Accept": YANG_JSON, **auth_headers}

        response = http2_client.get(
            f"{api_url}{SYSTEM}",
            headers=headers,
            params={"fields": "config/system-name"},
        )

        assert response.status_code == 200
        assert_yang_content_type(response)

        body = response_json(response)
        system = get_top(body, f"{MOD}:system")

        assert visible_keys(system) == {"config"}, (
            f"fields=config/system-name doit retourner uniquement system/config : {system}"
        )

        config = system["config"]
        assert visible_keys(config) == {"system-name"}, (
            f"system/config doit contenir uniquement system-name : {config}"
        )
        assert config["system-name"] == EXPECTED_SYSTEM_NAME

    def test_fields_list_child_key(self, http2_client, api_url, auth_headers, require_rt, require_fields):
        headers = {"Accept": YANG_JSON, **auth_headers}

        response = http2_client.get(
            f"{api_url}{INTERFACES}",
            headers=headers,
            params={"fields": "interface/name"},
        )

        assert response.status_code == 200
        assert_yang_content_type(response)

        body = response_json(response)
        interfaces = get_top(body, f"{MOD}:interfaces")

        assert "interface" in interfaces, f"interfaces/interface attendu : {interfaces}"
        entries = interfaces["interface"]
        assert isinstance(entries, list) and entries, f"interface[] vide ou invalide : {interfaces}"

        for entry in entries:
            keys = visible_keys(entry)
            assert keys == {"name"}, (
                f"fields=interface/name doit retourner uniquement la clé name : {entry}"
            )
            assert entry["name"] in EXPECTED_INTERFACES_BY_NAME, (
                f"Interface inattendue : {entry['name']!r}"
            )

    def test_fields_list_child_non_key(self, http2_client, api_url, auth_headers, require_rt, require_fields):
        headers = {"Accept": YANG_JSON, **auth_headers}

        response = http2_client.get(
            f"{api_url}{INTERFACES}",
            headers=headers,
            params={"fields": "interface/mtu"},
        )

        assert response.status_code == 200
        assert_yang_content_type(response)

        body = response_json(response)
        interfaces = get_top(body, f"{MOD}:interfaces")

        assert "interface" in interfaces
        entries = interfaces["interface"]
        assert isinstance(entries, list) and entries

        # Pour une liste, la clé doit rester présente même si on demande
        # seulement une feuille non-key.
        has_mtu = False
        for entry in entries:
            keys = visible_keys(entry)

            assert "name" in keys, f"La clé 'name' doit être présente : {entry}"
            assert keys <= {"name", "mtu"}, (
                f"fields=interface/mtu doit limiter les feuilles à name/mtu : {entry}"
            )

            if "mtu" in entry:
                has_mtu = True
                assert isinstance(entry["mtu"], int), f"mtu doit être un entier : {entry}"

        assert has_mtu, f"Aucun mtu retourné avec fields=interface/mtu : {entries}"

    def test_fields_multiple_nested_lists(self, http2_client, api_url, auth_headers, require_rt, require_fields):
        headers = {"Accept": YANG_JSON, **auth_headers}

        response = http2_client.get(
            f"{api_url}{INTERFACES}",
            headers=headers,
            params={"fields": "interface/name;vlan/vlan-id"},
        )

        assert response.status_code == 200
        assert_yang_content_type(response)

        body = response_json(response)
        interfaces = get_top(body, f"{MOD}:interfaces")

        assert "interface" in interfaces
        assert "vlan" in interfaces

        for entry in interfaces["interface"]:
            assert visible_keys(entry) == {"name"}, (
                f"interface/name doit retourner uniquement name : {entry}"
            )

        for entry in interfaces["vlan"]:
            assert visible_keys(entry) == {"vlan-id"}, (
                f"vlan/vlan-id doit retourner uniquement vlan-id : {entry}"
            )

    def test_fields_nested_container(self, http2_client, api_url, auth_headers, require_rt, require_fields):
        headers = {"Accept": YANG_JSON, **auth_headers}

        response = http2_client.get(
            f"{api_url}{INTERFACES}",
            headers=headers,
            params={"fields": "advanced-features/enable-advanced"},
        )

        assert response.status_code == 200
        assert_yang_content_type(response)

        body = response_json(response)
        interfaces = get_top(body, f"{MOD}:interfaces")

        assert "advanced-features" in interfaces, (
            f"advanced-features attendu : {interfaces}"
        )

        adv = interfaces["advanced-features"]
        assert visible_keys(adv) == {"enable-advanced"}, (
            f"fields=advanced-features/enable-advanced doit retourner uniquement cette feuille : {adv}"
        )
        assert adv["enable-advanced"] is True

    def test_fields_with_content_config(self, http2_client, api_url, auth_headers, require_rt, require_fields):
        headers = {"Accept": YANG_JSON, **auth_headers}

        response = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=headers,
            params={
                "content": "config",
                "fields": "device-id",
            },
        )

        assert response.status_code == 200
        assert_yang_content_type(response)

        body = response_json(response)
        data = get_top(body, f"{MOD}:basic-data")

        assert visible_keys(data) == {"device-id"}, (
            f"content=config&fields=device-id doit retourner uniquement device-id : {data}"
        )
        assert data["device-id"] == EXPECTED_BASIC_CONFIG["device-id"]
        assert "uptime" not in data


@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8.3")
class TestT_QUERY_07_FieldsInvalid:
    """
    T-QUERY-07 : GET avec fields invalide.

    Les valeurs invalides doivent produire une erreur RESTCONF avec
    error-tag=invalid-value. Le serveur peut répondre 400 ou 404 selon
    la façon dont il traite le nœud inexistant.
    """

    @pytest.mark.parametrize(
        "path,value",
        [
            # Champ inexistant
            (BASIC_DATA, "non-existent-field-xyz"),
            (BASIC_DATA, f"{MOD}:non-existent-field-xyz"),

            # Une feuille n'a pas d'enfant
            (BASIC_DATA, "device-id/nonexistent"),

            # Champ inexistant dans une liste
            (INTERFACES, "interface/nonexistent-child"),

            # Syntaxe invalide
            (BASIC_DATA, "device-id("),
            (BASIC_DATA, "("),
            (BASIC_DATA, ";"),
            (BASIC_DATA, "device-id;"),
            (BASIC_DATA, ";device-id"),
            (BASIC_DATA, "device-id;nonexistent"),
            (INTERFACES, "interface/"),
            (INTERFACES, "/"),
            (INTERFACES, "interface[name=eth0]"),
        ],
    )
    def test_fields_invalid(self, http2_client, api_url, auth_headers, require_rt, path, value):
        headers = {"Accept": YANG_JSON, **auth_headers}

        response = http2_client.get(
            f"{api_url}{path}",
            headers=headers,
            params={"fields": value},
        )

        assert_invalid_value_error(response, expected_statuses=(400, 404))

    def test_fields_select_nonconfig_with_content_config(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_rt,
    ):
        """
        content=config ne peut pas sélectionner une donnée config false.
        """
        headers = {"Accept": YANG_JSON, **auth_headers}

        response = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=headers,
            params={
                "content": "config",
                "fields": "uptime",
            },
        )

        assert_invalid_value_error(response, expected_statuses=(400, 404))

# ===========================================================================
# T-QUERY-08 à T-QUERY-11 : with-defaults (RFC 6243)
# ===========================================================================


@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8.9")
@pytest.mark.rfc("RFC 6243")
class TestT_QUERY_08_to_11_WithDefaults:
    """T-QUERY-08 : with-defaults=report-all.

    T-QUERY-09 : with-defaults=trim
    T-QUERY-10 : with-defaults=explicit
    T-QUERY-11 : with-defaults=report-all-tagged
    """

    @pytest.mark.parametrize(
        "mode",
        [
            "report-all",
            "trim",
            "explicit",
            "report-all-tagged",
        ],
    )
    def test_with_defaults_modes(self, http2_client, api_url, auth_headers, require_rt, mode):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=headers,
            params={"with-defaults": mode},
        )

        def validate(body: Any) -> None:
            data = get_top(body, f"{MOD}:basic-data")

            # device-id est mandatory et n'a pas de valeur par défaut.
            assert data.get("device-id") == EXPECTED_BASIC_CONFIG["device-id"], (
                f"device-id attendu avec with-defaults={mode} : {data}"
            )

            if mode in ("report-all", "report-all-tagged", "explicit"):
                # restconf-test.json positionne explicitement timeout=30 et
                # enabled=true. Dans ces modes, ils doivent être visibles.
                assert data.get("timeout") == EXPECTED_BASIC_CONFIG["timeout"], (
                    f"timeout doit être visible en mode {mode} : {data}"
                )
                assert data.get("enabled") == EXPECTED_BASIC_CONFIG["enabled"], (
                    f"enabled doit être visible en mode {mode} : {data}"
                )

            elif mode == "trim":
                # timeout et enabled valent leur défaut (30 / true). En mode
                # trim, ils doivent être omis.
                assert "timeout" not in data, (
                    f"timeout doit être masqué en mode trim : {data}"
                )
                assert "enabled" not in data, (
                    f"enabled doit être masqué en mode trim : {data}"
                )

        assert_ok_or_invalid_value_unsupported(response, validate)


# ===========================================================================
# T-QUERY-12 : GET avec paramètre de requête inconnu
# ===========================================================================


@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8")
class TestT_QUERY_12_UnknownParameter:
    """T-QUERY-12 : GET avec paramètre de requête inconnu.

    Un paramètre inattendu doit être rejeté avec 400 Bad Request et une
    enveloppe d'erreur RESTCONF.
    """

    @pytest.mark.parametrize(
        "params",
        [
            {"unknown-param-xyz": "some-value"},
            {"unknown": "1", "another-unknown": "123"},
        ],
    )
    def test_unknown_parameter(self, http2_client, api_url, auth_headers, require_rt, params):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=headers,
            params=params,
        )

        assert response.status_code == 400
        assert_invalid_value_error(response, expected_statuses=(400,))


# ===========================================================================
# T-QUERY-13 : GET avec combinaison content, depth, fields
# ===========================================================================


@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8")
class TestT_QUERY_13_CombinedParameters:
    """T-QUERY-13 : GET avec combinaison content, depth, fields.

    La réponse doit respecter simultanément les paramètres lorsque la
    combinaison est supportée.
    """

    def test_combined_parameters(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        params = {
            "content": "config",
            "depth": "1",
            "fields": "device-id",
        }
        response = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=headers,
            params=params,
        )

        def validate(body: Any) -> None:
            data = get_top(body, f"{MOD}:basic-data")
            keys = visible_keys(data)

            assert keys == {"device-id"}, (
                "La combinaison content=config&depth=1&fields=device-id "
                f"doit retourner uniquement device-id : {data}"
            )
            assert data["device-id"] == EXPECTED_BASIC_CONFIG["device-id"]
            assert "uptime" not in data

        assert_ok_or_invalid_value_unsupported(response, validate)


# ===========================================================================
# T-QUERY-14 : GET sur stream avec start-time / stop-time
# ===========================================================================


@pytest.mark.roadmap("R21")
@pytest.mark.rfc("RFC 8040 §4.8.7")
@pytest.mark.rfc("RFC 8040 §4.8.8")
class TestT_QUERY_14_StreamReplay:
    """T-QUERY-14 : GET sur stream avec start-time / stop-time.

    Accepté uniquement si le replay est supporté par le stream.
    """

    def test_stream_start_stop_time(self, http2_client, api_url, auth_headers):
        headers = {"Accept": EVENT_STREAM, **auth_headers}
        params = {
            "start-time": "2020-01-01T00:00:00Z",
            "stop-time": "2020-01-02T00:00:00Z",
        }
        response = http2_client.get(
            f"{api_url}{STREAM_PATH}",
            headers=headers,
            params=params,
        )

        # 200 si replay supporté, 400/404/501 sinon.
        assert response.status_code in (200, 400, 404, 501)

        if response.status_code == 200:
            assert get_content_type(response) == EVENT_STREAM, (
                "Un stream RESTCONF doit utiliser text/event-stream"
            )

        if response.status_code == 400 and get_content_type(response) == YANG_JSON:
            assert_invalid_value_error(response, expected_statuses=(400,))


# ===========================================================================
# T-QUERY-15 : GET sur stream sans replay mais avec start-time
# ===========================================================================


@pytest.mark.roadmap("R21")
@pytest.mark.rfc("RFC 8040 §4.8.7")
class TestT_QUERY_15_StreamNoReplay:
    """T-QUERY-15 : GET sur stream sans replay mais avec start-time.

    Si le stream existe mais ne supporte pas le replay, le serveur doit
    retourner une erreur RESTCONF. Si le stream n'existe pas, 404 est
    acceptable.
    """

    def test_stream_start_time_no_replay(self, http2_client, api_url, auth_headers):
        headers = {"Accept": EVENT_STREAM, **auth_headers}
        params = {"start-time": "2020-01-01T00:00:00Z"}
        response = http2_client.get(
            f"{api_url}{STREAM_PATH}",
            headers=headers,
            params=params,
        )

        assert response.status_code in (200, 400, 404, 501)

        if response.status_code == 200:
            assert get_content_type(response) == EVENT_STREAM

        if response.status_code == 400 and get_content_type(response) == YANG_JSON:
            assert_invalid_value_error(response, expected_statuses=(400,))


# ===========================================================================
# Erreurs de paramètres de requête
# ===========================================================================


@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8")
class TestQueryParameterValidationErrors:
    """Cas d'erreurs sur les paramètres de requête GET."""

    @pytest.mark.parametrize(
        "value",
        [
            "foo",
            "CONFIG",
            "non-config",
            "allx",
        ],
    )
    def test_invalid_content(self, http2_client, api_url, auth_headers, require_rt, value):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=headers,
            params={"content": value},
        )

        assert response.status_code == 400, (
            f"content={value!r} doit être rejeté avec 400, "
            f"status reçu : {response.status_code}"
        )
        assert_invalid_value_error(response, expected_statuses=(400,))

    @pytest.mark.parametrize(
        "value",
        [
            "0",
            "-1",
            "1.5",
            "UNBOUNDED",
            "unboundedx",
            "foo",
        ],
    )
    def test_invalid_depth(self, http2_client, api_url, auth_headers, require_rt, value):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=headers,
            params={"depth": value},
        )

        assert response.status_code == 400, (
            f"depth={value!r} doit être rejeté avec 400, "
            f"status reçu : {response.status_code}"
        )
        assert_invalid_value_error(response, expected_statuses=(400,))

    @pytest.mark.parametrize(
        "value",
        [
            "foo",
            "REPORT-ALL",
            "report_all",
            "report-all-taggedx",
        ],
    )
    def test_invalid_with_defaults(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_rt,
        value,
    ):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=headers,
            params={"with-defaults": value},
        )

        assert response.status_code == 400, (
            f"with-defaults={value!r} doit être rejeté avec 400, "
            f"status reçu : {response.status_code}"
        )
        assert_invalid_value_error(response, expected_statuses=(400,))

    def test_nonexistent_resource(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}/data/restconf-test:nonexistent-container",
            headers=headers,
            params={"content": "all"},
        )

        assert response.status_code == 404
        assert_invalid_value_error(response, expected_statuses=(404,))

    def test_content_nonconfig_on_config_only_resource(
        self,
        http2_client,
        api_url,
        auth_headers,
        require_rt,
    ):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{INTERFACES}",
            headers=headers,
            params={"content": "nonconfig"},
        )

        # interfaces ne contient pas de config false : soit le serveur répond
        # 404, soit il répond 200 avec une sélection vide.
        assert response.status_code in (200, 404)

        if response.status_code != 200:
            return

        assert_yang_content_type(response)

        body = response_json(response)
        interfaces = body.get(f"{MOD}:interfaces", {})

        # Aucune donnée config true ne doit fuir avec content=nonconfig.
        for key, value in interfaces.items():
            if isinstance(value, list):
                assert value == [], (
                    f"content=nonconfig ne doit pas retourner la liste {key} : {value}"
                )
            elif isinstance(value, dict):
                assert value == {}, (
                    f"content=nonconfig ne doit pas retourner le container {key} : {value}"
                )
            else:
                pytest.fail(
                    f"Contenu inattendu dans interfaces avec content=nonconfig : "
                    f"{key}={value!r}"
                )


# ===========================================================================
# Erreurs de paramètres start-time / stop-time sur les streams
# ===========================================================================


@pytest.mark.roadmap("R21")
@pytest.mark.rfc("RFC 8040 §4.8.7")
class TestStreamQueryErrors:
    """Cas d'erreurs sur start-time / stop-time."""

    @pytest.mark.parametrize(
        "params",
        [
            {"start-time": "not-a-date"},
            {"start-time": "2020-13-01T00:00:00Z"},
            {
                "start-time": "2020-01-02T00:00:00Z",
                "stop-time": "2020-01-01T00:00:00Z",
            },
        ],
    )
    def test_invalid_stream_times(self, http2_client, api_url, auth_headers, params):
        headers = {"Accept": EVENT_STREAM, **auth_headers}
        response = http2_client.get(
            f"{api_url}{STREAM_PATH}",
            headers=headers,
            params=params,
        )

        # Si le stream n'existe pas, 404 est acceptable. Si le serveur
        # valide la requête, il doit retourner 400 pour valeur invalide.
        assert response.status_code in (400, 404)

        if response.status_code == 400 and get_content_type(response) == YANG_JSON:
            assert_invalid_value_error(response, expected_statuses=(400,))

@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8")
class Test_QUERY_FilterValid:
    """
    Tests du query parameter 'filter'.

    La syntaxe exacte de 'filter' peut dépendre de l'implémentation.
    La fixture filter_syntax essaie plusieurs syntaxes courantes.
    """

    def test_filter_basic_leaf(self, http2_client, api_url, auth_headers, require_rt, require_filter):
        value = require_filter.get("basic_device_id")
        if not value:
            pytest.skip("Aucune syntaxe filter reconnue pour basic-data/device-id")

        headers = {"Accept": YANG_JSON, **auth_headers}

        response = http2_client.get(
            f"{api_url}{BASIC_DATA}",
            headers=headers,
            params={"filter": value},
        )

        assert response.status_code == 200
        assert_yang_content_type(response)

        body = response_json(response)

        # Le filtre doit sélectionner device-id uniquement.
        assert contains_value(body, EXPECTED_BASIC_CONFIG["device-id"]), (
            f"device-id attendu dans la réponse filtrée : {body}"
        )

        # Les autres feuilles de basic-data ne doivent pas être retournées.
        assert not contains_key(body, "timeout"), f"timeout ne doit pas être filtré : {body}"
        assert not contains_key(body, "enabled"), f"enabled ne doit pas être filtré : {body}"
        assert not contains_key(body, "uptime"), f"uptime ne doit pas être filtré : {body}"

    def test_filter_list_entry(self, http2_client, api_url, auth_headers, require_rt, require_filter):
        value = require_filter.get("interface_eth0")
        if not value:
            pytest.skip("Aucune syntaxe filter reconnue pour interface[name='eth0']")

        headers = {"Accept": YANG_JSON, **auth_headers}

        response = http2_client.get(
            f"{api_url}{INTERFACES}",
            headers=headers,
            params={"filter": value},
        )

        assert response.status_code == 200
        assert_yang_content_type(response)

        body = response_json(response)

        # Doit contenir eth0.
        assert contains_value(body, "eth0"), f"eth0 attendu dans la réponse filtrée : {body}"

        # Ne doit pas contenir les autres interfaces.
        assert not contains_value(body, "eth1"), f"eth1 ne doit pas être filtré : {body}"
        assert not contains_value(body, "lo0"), f"lo0 ne doit pas être filtré : {body}"

        # Le MTU 9000 appartient à eth1, il ne doit pas être présent.
        assert not contains_value(body, 9000), f"mtu 9000 de eth1 ne doit pas être présent : {body}"

        # Si la liste interface est présente, elle doit être limitée à eth0.
        if contains_key(body, "interface"):
            interfaces = body.get(f"{MOD}:interfaces", {})
            entries = interfaces.get("interface", [])

            assert isinstance(entries, list), f"interface doit être une liste : {interfaces}"
            assert len(entries) == 1, f"Une seule interface attendue : {entries}"

            entry = entries[0]
            assert isinstance(entry, dict)
            assert entry.get("name") == "eth0"


@pytest.mark.roadmap("R19")
@pytest.mark.rfc("RFC 8040 §4.8")
class Test_QUERY_FilterInvalid:
    """
    Cas d'erreurs pour le query parameter 'filter'.
    """

    @pytest.mark.parametrize(
        "path,value",
        [
            # Filtre inexistant
            (BASIC_DATA, "nonexistent"),
            (BASIC_DATA, f"/{MOD}:nonexistent"),
            (BASIC_DATA, f"{MOD}:basic-data/nonexistent"),

            # Filtre sur une feuille qui ne peut pas avoir d'enfants
            (BASIC_DATA, "device-id/nonexistent"),

            # Entrée de liste inexistante
            (INTERFACES, f"/{MOD}:interfaces/interface[name='nonexistent-if']"),
            (INTERFACES, "interface[name='nonexistent-if']"),

            # Syntaxe invalide
            (BASIC_DATA, "("),
            (BASIC_DATA, "((("),
            (BASIC_DATA, "="),
            (INTERFACES, "interface[name=eth0]"),
            (INTERFACES, "interface[name='eth0'"),
        ],
    )
    def test_filter_invalid(self, http2_client, api_url, auth_headers, require_rt, path, value):
        headers = {"Accept": YANG_JSON, **auth_headers}

        response = http2_client.get(
            f"{api_url}{path}",
            headers=headers,
            params={"filter": value},
        )

        assert_invalid_value_error(response, expected_statuses=(400, 404))