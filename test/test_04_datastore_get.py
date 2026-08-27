# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN
"""Tests de conformité : Datastore et lecture GET simple (section 4).

Couvre la section « 4. Datastore et lecture GET simple » de ``ROADMAP.md``
(items R5, R6, R7, R41, R43), soit les tests ``T-GET-01`` à ``T-GET-12``.

Ces tests s'appuient sur le module YANG de qualification ``restconf-test``
(namespace ``urn:restconf:test``, préfixe ``rt``) et sur le jeu de données
``test/restconf-test.json`` chargé dans le datastore. Contrairement au module
``oven`` (couvert par ``test_05_oven.py``), ``restconf-test`` déclare des
containers, des leafs, des listes à clés (chaîne et numérique), des listes
``ordered-by user``, des leaf-lists et des clés de liste contenant des
caractères spéciaux — ce qui permet de couvrir *réellement* les tests
T-GET-05 (listes), T-GET-06 (leaf-lists) et T-GET-11 (clés spéciales).

Le module doit être installé (voir ``docker/Dockerfile``) :

    sysrepoctl -i test/restconf-test.yang \
        --enable-feature=advanced-monitoring \
        --enable-feature=legacy-support
    sysrepocfg --import=test/restconf-test.json --format=json --datastore=startup

Seules les données de configuration de ``restconf-test.json`` sont exercées
ici : les nœuds ``config false`` (``system/state``, ``basic-data/uptime``) et
les RPC/actions de ``restconf-test`` nécessitent un plugin opérationnel qui
n'est pas fourni ; ils ne sont donc pas testés dans cette section.

Extraits du modèle et du jeu de données utilisés :

    container basic-data {
        leaf device-id { type device-name; mandatory true; }   // "dut-01"
        leaf timeout   { type uint16; default 30; }            // 30
        leaf enabled   { type boolean; default true; }         // true
    }
    container interfaces {
        list interface { key "name"; ... }                     // eth0/eth1/lo0
        list vlan      { key "vlan-id"; ... }                  // 100/200
    }
    container access-control {
        leaf-list allowed-ips { type inet:ipv4-address; }      // 3 entrées
        leaf-list tags        { type string; }                 // 3 entrées
    }
    container keyed-entries {
        list keyed-entry { key "label"; ... }                  // "with,comma",
                                                               // "with space"

Les tests sont fondés exclusivement sur les comportements attendus par les
RFC, et non sur une implémentation particulière :

- RFC 8040 §3.4   — *Datastore Resource* : ``{+restconf}/data`` expose la vue
                    du datastore conceptuel.
- RFC 8040 §4.3   — *GET* : lecture d'une ressource de données ; HEAD partage
                    la même sémantique sans le corps.
- RFC 8040 §3.5.3 — *api-path* : le chemin est décodé (percent-encoding) avant
                    traitement ; les clés de liste peuvent contenir des
                    caractères spéciaux (',' ' ' …) percent-encodés.
- RFC 8040 §7     — *Error Reporting* : les erreurs sont renvoyées dans
                    l'enveloppe ``ietf-restconf:errors``.
- RFC 7951        — *JSON Encoding of YANG Data* : encodage JSON des nœuds
                    YANG. Le nœud de plus haut niveau est nommé
                    ``module:name`` ; les nœuds enfants appartenant au *même*
                    module ne sont PAS préfixés (RFC 7951 §4). Les boolean
                    sont des booléens JSON (§6.7), les entiers des nombres
                    JSON (§6.2), les listes des tableaux (§7.5) et les
                    leaf-lists des tableaux (§7.7).
- RFC 7950        — *YANG 1.1* : encodage XML, namespace du module.
- RFC 9110 §9.3.2 — *HEAD* : mêmes en-têtes que GET, corps vide.
- RFC 3986        — *URI Generic Syntax* : percent-encoding.
"""

import xml.etree.ElementTree as ET
from urllib.parse import quote

import pytest

# ---------------------------------------------------------------------------
# Références normatives (constantes dérivées des RFC / du module restconf-test)
# ---------------------------------------------------------------------------

# Media types RESTCONF (RFC 8040 §4.2).
YANG_JSON = "application/yang-data+json"
YANG_XML = "application/yang-data+xml"

# Namespace XML du module restconf-test (RFC 7950 §7.1.2) et de l'enveloppe
# d'erreur RESTCONF (RFC 8040 §7).
RT_NS = "urn:restconf:test"
RESTCONF_NS = "urn:ietf:params:xml:ns:yang:ietf-restconf"

# Préfixe JSON du module (RFC 7951 §4 : nœud de plus haut niveau préfixé).
MOD = "restconf-test"

# Chemins RESTCONF (api-path, RFC 8040 §3.5) construits sur le module.
DATA_PATH = "/data"
BASIC_DATA = "/data/restconf-test:basic-data"
SYSTEM_CONFIG = "/data/restconf-test:system/config"
INTERFACES = "/data/restconf-test:interfaces"
INTERFACE_LIST = "/data/restconf-test:interfaces/interface"
VLAN_LIST = "/data/restconf-test:interfaces/vlan"
ACCESS_CONTROL = "/data/restconf-test:access-control"
ALLOWED_IPS = "/data/restconf-test:access-control/allowed-ips"
TAGS = "/data/restconf-test:access-control/tags"
KEYED_ENTRIES = "/data/restconf-test:keyed-entries"
MULTI_KEYED_ENTRIES = "/data/restconf-test:keyed-entries/multi-keyed-entry"

# Jeu de données attendu (issu de test/restconf-test.json).
INTERFACE_NAMES = {"eth0", "eth1", "lo0"}
VLAN_IDS = {100, 200}
ALLOWED_IP_VALUES = {"192.0.2.1", "198.51.100.7", "203.0.113.9"}
TAG_VALUES = {"production", "edge", "restconf"}

# Clés de liste contenant des caractères spéciaux (T-GET-11). La virgule
# sépare les clés dans un api-path (RFC 8040 §3.5.3) et l'espace n'est pas
# autorisé brut dans un chemin (RFC 3986) : tous deux doivent être
# percent-encodés sur le fil puis décodés par le serveur.
SPECIAL_KEYS = {
    "with,comma": "key contains a comma",
    "with space": "key contains a space",
}

# Entrées de la liste à deux clés ``multi-keyed-entry`` (T-GET-11, clés
# multiples, Errata RFC 8040 EID 5255). Clé = tuple (region, label).
MULTI_KEYS = {
    ("eu", "alpha"): "eu/alpha entry",
    ("eu", "with,comma"): "second key contains a comma",
    ("us", "beta"): "us/beta entry",
}


# ---------------------------------------------------------------------------
# Utilitaires de parsing et d'assertion (fondés sur les RFC)
# ---------------------------------------------------------------------------

def get_content_type(response) -> str:
    """Retourne le Content-Type de la réponse, sans ses paramètres (charset…)."""
    return response.headers.get("content-type", "").split(";")[0].strip().lower()


def assert_json_data_envelope(body, top_node: str):
    """Vérifie qu'un corps JSON YANG est un objet dont la racine est ``top_node``.

    RFC 7951 §4 : le document JSON est un objet ; le nœud de premier niveau
    est nommé ``module:name``.
    """
    assert isinstance(body, dict), (
        f"le corps JSON YANG doit être un objet (RFC 7951 §4), obtenu {type(body)}"
    )
    assert top_node in body, (
        f"le nœud de premier niveau {top_node!r} est attendu (RFC 7951 §4), "
        f"obtenu {sorted(body.keys())!r} {body}"
    )


def assert_json_leaf_encoding(body, leaf: str, py_type):
    """Vérifie l'encodage JSON d'une leaf lue directement (RFC 7951).

    Pour un GET ciblant une leaf, le nœud de premier niveau est préfixé
    ``module:leaf`` (RFC 7951 §4) et la valeur a le type Python attendu :
    ``bool`` pour un boolean YANG (§6.7), ``int`` pour un entier (§6.2),
    ``str`` pour une chaîne (§6.1).
    """
    key = f"{MOD}:{leaf}"
    assert key in body, (
        f"la leaf {key!r} est attendue en racine JSON (RFC 7951 §4), "
        f"obtenu {sorted(body.keys())!r}"
    )
    value = body[key]
    # bool est une sous-classe de int : on distingue explicitement les deux.
    if py_type is bool:
        assert isinstance(value, bool), (
            f"la leaf boolean {key!r} doit être encodée en booléen JSON "
            f"(RFC 7951 §6.7), obtenu {value!r} ({type(value).__name__})"
        )
    elif py_type is int:
        assert isinstance(value, int) and not isinstance(value, bool), (
            f"la leaf entière {key!r} doit être encodée en nombre JSON "
            f"(RFC 7951 §6.2), obtenu {value!r} ({type(value).__name__})"
        )
    elif py_type is str:
        assert isinstance(value, str), (
            f"la leaf chaîne {key!r} doit être encodée en chaîne JSON "
            f"(RFC 7951 §6.1), obtenu {value!r} ({type(value).__name__})"
        )


def assert_json_error_envelope(body):
    """Vérifie la structure de l'enveloppe d'erreur JSON (RFC 8040 §7).

    ``{"ietf-restconf:errors": {"error": [{"error-type": ..., "error-tag": ...}]}}``
    où ``error-type`` et ``error-tag`` sont obligatoires (RFC 8040 §7.1).
    """
    assert isinstance(body, dict), "l'enveloppe d'erreur doit être un objet JSON"
    assert "ietf-restconf:errors" in body, (
        f"l'enveloppe d'erreur doit contenir 'ietf-restconf:errors' "
        f"(RFC 8040 §7), obtenu {sorted(body.keys())!r}"
    )
    errors = body["ietf-restconf:errors"]
    assert isinstance(errors, dict) and isinstance(errors.get("error"), list), (
        f"'ietf-restconf:errors' doit contenir une liste 'error' "
        f"(RFC 8040 §7), obtenu {errors!r}"
    )
    assert errors["error"], "la liste 'error' ne doit pas être vide"
    for error in errors["error"]:
        assert isinstance(error, dict)
        assert "error-type" in error, (
            f"'error-type' est obligatoire dans chaque erreur (RFC 8040 §7.1) : {error!r}"
        )
        assert "error-tag" in error, (
            f"'error-tag' est obligatoire dans chaque erreur (RFC 8040 §7.1) : {error!r}"
        )
        assert error["error-type"] in ("transport", "rpc", "protocol", "application"), (
            f"'error-type' doit être l'une des valeurs de RFC 8040 §7.1, "
            f"obtenu {error['error-type']!r}"
        )


def assert_xml_error_envelope(text: str):
    """Vérifie la structure de l'enveloppe d'erreur XML (RFC 8040 §7)."""
    root = ET.fromstring(text)
    assert root.tag == f"{{{RESTCONF_NS}}}errors", (
        f"la racine de l'enveloppe d'erreur XML doit être "
        f"{{{RESTCONF_NS}}}errors (RFC 8040 §7), obtenu {root.tag!r}"
    )
    error_elems = root.findall(f"{{{RESTCONF_NS}}}error")
    assert error_elems, "l'enveloppe d'erreur XML doit contenir au moins un <error>"
    for error in error_elems:
        assert error.find(f"{{{RESTCONF_NS}}}error-type") is not None, (
            "'error-type' est obligatoire dans chaque erreur (RFC 8040 §7.1)"
        )
        assert error.find(f"{{{RESTCONF_NS}}}error-tag") is not None, (
            "'error-tag' est obligatoire dans chaque erreur (RFC 8040 §7.1)"
        )


def local_name(tag: str) -> str:
    """Retourne le nom local d'un tag XML ``{ns}name``."""
    return tag.split("}", 1)[-1]


# ---------------------------------------------------------------------------
# Fixtures : disponibilité du module restconf-test et de ses données
# ---------------------------------------------------------------------------

# Chemins sondés : des containers de configuration renseignés par
# restconf-test.json. Si le module n'est pas installé ou si le jeu de données
# n'est pas chargé, tous ces chemins retournent 404.
_RT_PROBE_PATHS = (BASIC_DATA, INTERFACES)


@pytest.fixture(scope="session")
def rt_probe(http2_client, api_url, auth_headers, test_jwt):
    """Sonde l'accessibilité du module ``restconf-test`` sur le datastore.

    Retourne un dict ``{chemin: réponse}`` pour les chemins sondés, ou
    ``None`` si aucun JWT n'est configuré (authentification requise).
    """
    if not test_jwt:
        return None
    headers = {"Accept": YANG_JSON, **auth_headers}
    return {
        path: http2_client.get(f"{api_url}{path}", headers=headers)
        for path in _RT_PROBE_PATHS
    }


@pytest.fixture()
def require_rt(rt_probe):
    """Skip le test si le module ``restconf-test`` n'est pas installé/accessible.

    L'installation du module et le chargement de ``restconf-test.json`` sont
    des prérequis d'environnement (Dockerfile), pas des comportements testés :
    si aucune donnée n'est accessible, le test est ignoré plutôt qu'échoué.
    """
    if rt_probe is None:
        pytest.skip(
            "Aucun JWT configuré — accès au datastore restconf-test impossible "
            "(--restconf-test-jwt=<token> ou RESTCONF_TEST_JWT)"
        )

    statuses = {path: resp.status_code for path, resp in rt_probe.items()}

    if any(resp.status_code == 200 for resp in rt_probe.values()):
        return rt_probe

    if all(resp.status_code == 404 for resp in rt_probe.values()):
        pytest.skip(
            f"Module YANG 'restconf-test' non installé ou données non chargées "
            f"({statuses})"
        )
    if all(resp.status_code in (401, 403) for resp in rt_probe.values()):
        pytest.skip(f"Accès au datastore restconf-test refusé ({statuses})")
    pytest.skip(f"Datastore restconf-test inaccessible ({statuses})")


# ============================================================================
# T-GET-01 : GET sur {+restconf}/data
# ============================================================================


@pytest.mark.roadmap("R5")
@pytest.mark.rfc("RFC 8040 §3.4")
@pytest.mark.rfc("RFC 7951")
class TestT_GET_01_DatastoreRoot:
    """
    T-GET-01 : GET sur {+restconf}/data retourne la vue du datastore.

    RFC 8040 §3.4 : « The RESTCONF root resource ... {+restconf}/data ...
    represents the conceptual datastore. »

    Comportement attendu :
    - ``200 OK`` (ou ``401``/``403`` si l'authentification fait défaut) ;
    - ``Content-Type`` conforme au media type négocié ;
    - en JSON, racine ``ietf-restconf:data`` (RFC 8040 §3.4.1, RFC 7951) ;
    - en XML, racine ``{urn:ietf:params:xml:ns:yang:ietf-restconf}data``.
    """

    def test_get_data_json(self, http2_client, api_url, auth_headers, require_jwt):
        """GET /data en JSON : racine ``ietf-restconf:data``."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{DATA_PATH}", headers=headers)

        assert response.status_code in (200, 401, 403), (
            f"GET {api_url}{DATA_PATH} doit retourner 200, 401 ou 403, "
            f"obtenu {response.status_code}"
        )
        if response.status_code != 200:
            pytest.skip(f"Accès au datastore refusé ({response.status_code})")

        assert get_content_type(response) == YANG_JSON, (
            f"Content-Type attendu {YANG_JSON!r}, "
            f"obtenu {response.headers.get('content-type')!r}"
        )

        body = response.json()
        assert_json_data_envelope(body, "ietf-restconf:data")
        # La vue du datastore est un objet (éventuellement vide si NACM masque
        # tout ou si aucune donnée n'est configurée).
        assert isinstance(body["ietf-restconf:data"], dict), (
            "'ietf-restconf:data' doit être un objet JSON"
        )

    def test_get_data_xml(self, http2_client, api_url, auth_headers, require_jwt):
        """GET /data en XML : racine ``{...ietf-restconf}data``."""
        headers = {"Accept": YANG_XML, **auth_headers}
        response = http2_client.get(f"{api_url}{DATA_PATH}", headers=headers)

        assert response.status_code in (200, 401, 403), (
            f"GET {api_url}{DATA_PATH} doit retourner 200, 401 ou 403, "
            f"obtenu {response.status_code}"
        )
        if response.status_code != 200:
            pytest.skip(f"Accès au datastore refusé ({response.status_code})")

        assert get_content_type(response) == YANG_XML, (
            f"Content-Type attendu {YANG_XML!r}, "
            f"obtenu {response.headers.get('content-type')!r}"
        )

        root = ET.fromstring(response.text)
        assert root.tag == f"{{{RESTCONF_NS}}}data", (
            f"la racine XML de /data doit être {{{RESTCONF_NS}}}data, "
            f"obtenu {root.tag!r}"
        )

    def test_data_contains_restconf_test_module(
        self, http2_client, api_url, auth_headers, require_rt
    ):
        """La vue du datastore inclut les données du module restconf-test.

        RFC 8040 §3.4 : ``/data`` expose l'ensemble du datastore conceptuel ;
        les nœuds de configuration chargés (``restconf-test:basic-data``)
        doivent y figurer (sous réserve de NACM et de with-defaults).
        """
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{DATA_PATH}", headers=headers)
        assert response.status_code == 200

        data = response.json()["ietf-restconf:data"]
        assert isinstance(data, dict)
        # basic-data contient une leaf mandatory (device-id) : toujours présente.
        assert f"{MOD}:basic-data" in data, (
            f"la vue du datastore doit contenir '{MOD}:basic-data' "
            f"(données chargées depuis restconf-test.json), "
            f"obtenu {sorted(data.keys())!r}"
        )


# ============================================================================
# T-GET-02 : GET sur un container
# ============================================================================


@pytest.mark.roadmap("R6")
@pytest.mark.rfc("RFC 8040 §4.3")
@pytest.mark.rfc("RFC 7951")
@pytest.mark.rfc("RFC 7950")
class TestT_GET_02_GetContainer:
    """
    T-GET-02 : GET sur un container : réponse 200 avec encodage correct.

    RFC 8040 §4.3 : lecture d'une ressource de données de type container.
    RFC 7951 §4 : le nœud de premier niveau est préfixé ``module:name`` ; les
    enfants du même module ne sont pas préfixés.

    Containers exercés (tous renseignés par restconf-test.json) :
    - ``restconf-test:basic-data`` (contient la leaf mandatory ``device-id``) ;
    - ``restconf-test:system/config`` (container à présence) ;
    - ``restconf-test:interfaces`` (contient les listes ``interface``/``vlan``).
    """

    def test_get_basic_data_json(self, http2_client, api_url, auth_headers, require_rt):
        """GET basic-data en JSON : racine ``restconf-test:basic-data``."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers)

        assert response.status_code == 200, (
            f"GET {api_url}{BASIC_DATA} doit retourner 200 (container présent "
            f"dans restconf-test.json), obtenu {response.status_code}"
        )
        assert get_content_type(response) == YANG_JSON

        body = response.json()
        assert_json_data_envelope(body, f"{MOD}:basic-data")

        container = body[f"{MOD}:basic-data"]
        assert isinstance(container, dict), "le container doit être un objet JSON"
        # device-id est mandatory : toujours présent, non préfixé (même module).
        assert "device-id" in container, (
            f"basic-data doit contenir 'device-id' (leaf mandatory, RFC 7950 "
            f"§7.6.5), obtenu {sorted(container.keys())!r}"
        )
        assert container["device-id"] == "dut-01", (
            f"basic-data/device-id doit valoir 'dut-01' (restconf-test.json), "
            f"obtenu {container['device-id']!r}"
        )
        # Les leafs à default, si reportées, doivent être correctement typées.
        if "timeout" in container:
            assert isinstance(container["timeout"], int) and not isinstance(
                container["timeout"], bool
            ), f"basic-data/timeout doit être un entier JSON, obtenu {container['timeout']!r}"
        if "enabled" in container:
            assert isinstance(container["enabled"], bool), (
                f"basic-data/enabled doit être un booléen JSON, "
                f"obtenu {container['enabled']!r}"
            )

    def test_get_basic_data_xml(self, http2_client, api_url, auth_headers, require_rt):
        """GET basic-data en XML : racine ``{urn:restconf:test}basic-data``."""
        headers = {"Accept": YANG_XML, **auth_headers}
        response = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers)

        assert response.status_code == 200, (
            f"GET {api_url}{BASIC_DATA} doit retourner 200, "
            f"obtenu {response.status_code}"
        )
        assert get_content_type(response) == YANG_XML

        root = ET.fromstring(response.text)
        assert root.tag == f"{{{RT_NS}}}basic-data", (
            f"la racine XML doit être {{{RT_NS}}}basic-data (RFC 7950), "
            f"obtenu {root.tag!r}"
        )
        device_id = root.find(f"{{{RT_NS}}}device-id")
        assert device_id is not None, "basic-data doit contenir <device-id>"
        assert device_id.text == "dut-01", (
            f"basic-data/device-id doit valoir 'dut-01', obtenu {device_id.text!r}"
        )

    def test_get_presence_container(self, http2_client, api_url, auth_headers, require_rt):
        """GET system/config (container à présence) en JSON."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{SYSTEM_CONFIG}", headers=headers)

        assert response.status_code == 200, (
            f"GET {api_url}{SYSTEM_CONFIG} doit retourner 200 (container à "
            f"présence configuré), obtenu {response.status_code}"
        )
        body = response.json()
        assert_json_data_envelope(body, f"{MOD}:config")
        config = body[f"{MOD}:config"]
        assert isinstance(config, dict)
        assert config.get("system-name") == "restconf-dut", (
            f"system/config/system-name doit valoir 'restconf-dut', "
            f"obtenu {config.get('system-name')!r}"
        )

    def test_get_interfaces_container_xml(
        self, http2_client, api_url, auth_headers, require_rt
    ):
        """GET interfaces en XML : racine unique ``{urn:restconf:test}interfaces``."""
        headers = {"Accept": YANG_XML, **auth_headers}
        response = http2_client.get(f"{api_url}{INTERFACES}", headers=headers)

        assert response.status_code == 200
        assert get_content_type(response) == YANG_XML

        root = ET.fromstring(response.text)
        assert root.tag == f"{{{RT_NS}}}interfaces", (
            f"la racine XML doit être {{{RT_NS}}}interfaces, obtenu {root.tag!r}"
        )


# ============================================================================
# T-GET-03 : GET sur une leaf
# ============================================================================


@pytest.mark.roadmap("R6")
@pytest.mark.rfc("RFC 8040 §4.3")
@pytest.mark.rfc("RFC 7951 §6")
class TestT_GET_03_GetLeaf:
    """
    T-GET-03 : GET sur une leaf : réponse 200 avec la valeur encodée.

    RFC 8040 §4.3 : lecture d'une leaf.
    RFC 7951 §6.1 : les chaînes sont des chaînes JSON.
    RFC 7951 §6.2 : les entiers YANG sont des nombres JSON.
    RFC 7951 §6.7 : les boolean YANG sont des booléens JSON.

    La leaf mandatory ``device-id`` (sans default) est toujours reportée →
    ``200``. Les leafs ``timeout``/``enabled`` portent des valeurs par défaut :
    si elles ne sont pas reportées (RFC 6243, basic-mode ``trim``/``explicit``),
    un ``404`` est acceptable.
    """

    def test_get_mandatory_string_leaf(self, http2_client, api_url, auth_headers, require_rt):
        """GET basic-data/device-id (string, mandatory) : 200 + valeur."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{BASIC_DATA}/device-id", headers=headers)

        assert response.status_code == 200, (
            f"GET {BASIC_DATA}/device-id (leaf mandatory) doit retourner 200, "
            f"obtenu {response.status_code}"
        )
        assert get_content_type(response) == YANG_JSON
        body = response.json()
        assert_json_leaf_encoding(body, "device-id", str)
        assert body[f"{MOD}:device-id"] == "dut-01", (
            f"device-id doit valoir 'dut-01', obtenu {body[f'{MOD}:device-id']!r}"
        )

    @pytest.mark.parametrize(
        "leaf,py_type,expected",
        [
            ("timeout", int, 30),     # uint16, default 30
            ("enabled", bool, True),  # boolean, default true
        ],
    )
    def test_get_default_leaf(
        self, http2_client, api_url, auth_headers, require_rt, leaf, py_type, expected
    ):
        """GET sur une leaf à default : 200 (valeur) ou 404 (default non reportée)."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{BASIC_DATA}/{leaf}", headers=headers)

        assert response.status_code in (200, 404), (
            f"GET {BASIC_DATA}/{leaf} doit retourner 200 ou 404, "
            f"obtenu {response.status_code}"
        )
        if response.status_code == 404:
            return  # leaf par défaut non reportée (RFC 6243) : comportement valide

        assert get_content_type(response) == YANG_JSON
        body = response.json()
        assert_json_leaf_encoding(body, leaf, py_type)
        assert body[f"{MOD}:{leaf}"] == expected, (
            f"{leaf} doit valoir {expected!r} (restconf-test.json), "
            f"obtenu {body[f'{MOD}:{leaf}']!r}"
        )

    def test_get_leaf_xml(self, http2_client, api_url, auth_headers, require_rt):
        """GET sur une leaf en XML : racine ``{urn:restconf:test}device-id``."""
        headers = {"Accept": YANG_XML, **auth_headers}
        response = http2_client.get(f"{api_url}{BASIC_DATA}/device-id", headers=headers)

        assert response.status_code == 200
        assert get_content_type(response) == YANG_XML

        root = ET.fromstring(response.text)
        assert root.tag == f"{{{RT_NS}}}device-id", (
            f"la racine XML d'une leaf doit être {{{RT_NS}}}device-id, "
            f"obtenu {root.tag!r}"
        )
        assert root.text == "dut-01", (
            f"device-id doit valoir 'dut-01' en XML, obtenu {root.text!r}"
        )


# ============================================================================
# T-GET-04 : HEAD sur une ressource
# ============================================================================


@pytest.mark.roadmap("R6")
@pytest.mark.rfc("RFC 8040 §4.3")
@pytest.mark.rfc("RFC 9110 §9.3.2")
class TestT_GET_04_Head:
    """
    T-GET-04 : HEAD sur une ressource : mêmes en-têtes que GET, sans corps.

    RFC 9110 §9.3.2 : « The HEAD method is identical to GET except that the
    server MUST NOT send content in the response. »
    RFC 8040 §4.3 : HEAD partage les mêmes règles que GET.
    """

    def test_head_no_body(self, http2_client, api_url, auth_headers, require_rt):
        """HEAD ne retourne aucun corps."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.head(f"{api_url}{BASIC_DATA}", headers=headers)

        assert response.status_code in (200, 401, 403), (
            f"HEAD {BASIC_DATA} doit retourner 200, 401 ou 403, "
            f"obtenu {response.status_code}"
        )
        # Quel que soit le statut, HEAD ne doit pas contenir de corps.
        assert response.content == b"", (
            f"HEAD ne doit retourner aucun corps (RFC 9110 §9.3.2), "
            f"obtenu {response.content!r}"
        )

    def test_head_headers_consistent_with_get(
        self, http2_client, api_url, auth_headers, require_rt
    ):
        """HEAD et GET retournent les mêmes en-têtes de représentation."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        target = f"{api_url}{BASIC_DATA}"

        get_resp = http2_client.get(target, headers=headers)
        head_resp = http2_client.head(target, headers=headers)

        assert get_resp.status_code == head_resp.status_code, (
            f"HEAD et GET doivent retourner le même statut "
            f"(RFC 9110 §9.3.2) : GET={get_resp.status_code} "
            f"HEAD={head_resp.status_code}"
        )
        if get_resp.status_code != 200:
            pytest.skip(f"Accès refusé ({get_resp.status_code})")

        assert get_content_type(head_resp) == get_content_type(get_resp), (
            f"HEAD et GET doivent annoncer le même Content-Type : "
            f"GET={get_resp.headers.get('content-type')!r} "
            f"HEAD={head_resp.headers.get('content-type')!r}"
        )

        # Les validateurs conditionnels, s'ils sont émis, doivent être cohérents
        # entre GET et HEAD (RFC 8040 §3.4.1, RFC 9110 §8.8).
        for validator in ("etag", "last-modified"):
            get_val = get_resp.headers.get(validator)
            head_val = head_resp.headers.get(validator)
            if get_val is not None or head_val is not None:
                assert get_val == head_val, (
                    f"l'en-tête {validator!r} doit être identique pour GET et "
                    f"HEAD : GET={get_val!r} HEAD={head_val!r}"
                )


# ============================================================================
# T-GET-05 : GET sur une liste
# ============================================================================


@pytest.mark.roadmap("R7")
@pytest.mark.rfc("RFC 8040 §4.3")
@pytest.mark.rfc("RFC 7951 §7.5")
class TestT_GET_05_GetList:
    """
    T-GET-05 : GET sur une liste : tableau JSON (RFC 7951 §7.5) / élément
    racine unique en XML (RFC 8040 §4.3).

    Listes exercées (renseignées par restconf-test.json) :
    - ``interfaces/interface`` (clé chaîne ``name`` : eth0/eth1/lo0) ;
    - ``interfaces/vlan`` (clé numérique ``vlan-id`` : 100/200).
    """

    def test_get_interface_list_json(self, http2_client, api_url, auth_headers, require_rt):
        """GET interface (liste entière) en JSON : tableau de 3 entrées."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{INTERFACE_LIST}", headers=headers)

        assert response.status_code == 200, (
            f"GET {api_url}{INTERFACE_LIST} doit retourner 200, "
            f"obtenu {response.status_code}"
        )
        assert get_content_type(response) == YANG_JSON

        body = response.json()
        assert_json_data_envelope(body, f"{MOD}:interface")

        entries = body[f"{MOD}:interface"]
        assert isinstance(entries, list), (
            f"une liste YANG doit être encodée en tableau JSON "
            f"(RFC 7951 §7.5), obtenu {type(entries).__name__}"
        )
        assert len(entries) == 3, (
            f"interfaces/interface doit contenir 3 entrées "
            f"(restconf-test.json), obtenu {len(entries)}"
        )
        # Chaque entrée est un objet dont la clé 'name' (même module, non
        # préfixée — RFC 7951 §4) est présente.
        names = set()
        for entry in entries:
            assert isinstance(entry, dict), "chaque entrée de liste doit être un objet"
            assert "name" in entry, (
                f"chaque entrée de 'interface' doit porter sa clé 'name', "
                f"obtenu {sorted(entry.keys())!r}"
            )
            names.add(entry["name"])
        assert names == INTERFACE_NAMES, (
            f"les clés de 'interface' doivent être {sorted(INTERFACE_NAMES)}, "
            f"obtenu {sorted(names)}"
        )

    def test_get_vlan_list_json(self, http2_client, api_url, auth_headers, require_rt):
        """GET vlan (clé numérique) en JSON : tableau de 2 entrées."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{VLAN_LIST}", headers=headers)

        assert response.status_code == 200
        body = response.json()
        assert_json_data_envelope(body, f"{MOD}:vlan")

        entries = body[f"{MOD}:vlan"]
        assert isinstance(entries, list) and len(entries) == 2, (
            f"interfaces/vlan doit contenir 2 entrées, obtenu "
            f"{len(entries) if isinstance(entries, list) else entries!r}"
        )
        vlan_ids = set()
        for entry in entries:
            assert "vlan-id" in entry, "chaque entrée 'vlan' doit porter 'vlan-id'"
            # La clé vlan-id est un uint16 → nombre JSON (RFC 7951 §6.2).
            assert isinstance(entry["vlan-id"], int) and not isinstance(
                entry["vlan-id"], bool
            ), f"vlan-id doit être un entier JSON, obtenu {entry['vlan-id']!r}"
            vlan_ids.add(entry["vlan-id"])
        assert vlan_ids == VLAN_IDS, (
            f"les clés de 'vlan' doivent être {sorted(VLAN_IDS)}, "
            f"obtenu {sorted(vlan_ids)}"
        )

    def test_get_single_list_instance_xml(
        self, http2_client, api_url, auth_headers, require_rt
    ):
        """GET d'une instance de liste en XML : élément racine unique.

        RFC 8040 §4.3 : la réponse XML d'une ressource de données contient un
        seul élément racine. Pour l'instance ``interface=eth0``, la racine est
        ``{urn:restconf:test}interface``.
        """
        headers = {"Accept": YANG_XML, **auth_headers}
        response = http2_client.get(
            f"{api_url}{INTERFACE_LIST}=eth0", headers=headers
        )

        assert response.status_code == 200, (
            f"GET {INTERFACE_LIST}=eth0 doit retourner 200, "
            f"obtenu {response.status_code}"
        )
        assert get_content_type(response) == YANG_XML

        root = ET.fromstring(response.text)  # lève ParseError si mal formé
        assert root.tag == f"{{{RT_NS}}}interface", (
            f"la racine XML d'une instance de liste doit être "
            f"{{{RT_NS}}}interface, obtenu {root.tag!r}"
        )
        name = root.find(f"{{{RT_NS}}}name")
        assert name is not None and name.text == "eth0", (
            f"l'instance interface=eth0 doit porter <name>eth0</name>, "
            f"obtenu {name.text if name is not None else None!r}"
        )

    def test_get_whole_list_xml_single_root(
        self, http2_client, api_url, auth_headers, require_rt
    ):
        """GET d'une liste entière en XML : document bien formé à racine unique.

        RFC 8040 §4.3 : même pour une collection, la réponse XML doit être un
        document bien formé avec un seul élément racine.
        """
        headers = {"Accept": YANG_XML, **auth_headers}
        response = http2_client.get(f"{api_url}{INTERFACE_LIST}", headers=headers)

        assert response.status_code in (200, 400)
        assert get_content_type(response) == YANG_XML

        # case multiple elem in list
        if response.status_code == 400:
            return

        # ET.fromstring exige une racine unique : échoue si plusieurs éléments
        # de premier niveau (document mal formé).
        root = ET.fromstring(response.text)
        assert root.tag.startswith(f"{{{RT_NS}}}"), (
            f"la racine XML de la liste doit être dans le namespace "
            f"{RT_NS!r}, obtenu {root.tag!r}"
        )


# ============================================================================
# T-GET-06 : GET sur une leaf-list
# ============================================================================


@pytest.mark.roadmap("R7")
@pytest.mark.rfc("RFC 8040 §4.3")
@pytest.mark.rfc("RFC 7951 §7.7")
class TestT_GET_06_GetLeafList:
    """
    T-GET-06 : GET sur une leaf-list : tableau JSON (RFC 7951 §7.7).

    Leaf-lists exercées (renseignées par restconf-test.json) :
    - ``access-control/allowed-ips`` (leaf-list ``inet:ipv4-address``, 3) ;
    - ``access-control/tags`` (leaf-list ``string``, 3).
    """

    def test_get_allowed_ips_json(self, http2_client, api_url, auth_headers, require_rt):
        """GET allowed-ips en JSON : tableau de chaînes."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{ALLOWED_IPS}", headers=headers)

        assert response.status_code == 200, (
            f"GET {api_url}{ALLOWED_IPS} doit retourner 200, "
            f"obtenu {response.status_code}"
        )
        assert get_content_type(response) == YANG_JSON

        body = response.json()
        assert_json_data_envelope(body, f"{MOD}:allowed-ips")

        values = body[f"{MOD}:allowed-ips"]
        assert isinstance(values, list), (
            f"une leaf-list doit être encodée en tableau JSON "
            f"(RFC 7951 §7.7), obtenu {type(values).__name__}"
        )
        assert set(values) == ALLOWED_IP_VALUES, (
            f"allowed-ips doit contenir {sorted(ALLOWED_IP_VALUES)}, "
            f"obtenu {sorted(values)!r}"
        )
        for value in values:
            assert isinstance(value, str), (
                f"chaque entrée de allowed-ips doit être une chaîne JSON, "
                f"obtenu {value!r}"
            )

    def test_get_tags_json(self, http2_client, api_url, auth_headers, require_rt):
        """GET tags en JSON : tableau de chaînes."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{TAGS}", headers=headers)

        assert response.status_code == 200
        body = response.json()
        assert_json_data_envelope(body, f"{MOD}:tags")

        values = body[f"{MOD}:tags"]
        assert isinstance(values, list) and set(values) == TAG_VALUES, (
            f"tags doit contenir {sorted(TAG_VALUES)}, obtenu {values!r}"
        )

    def test_get_leaf_list_xml(self, http2_client, api_url, auth_headers, require_rt):
        """GET d'une leaf-list en XML : document bien formé, namespace du module."""
        headers = {"Accept": YANG_XML, **auth_headers}
        response = http2_client.get(f"{api_url}{ALLOWED_IPS}", headers=headers)

        assert response.status_code in (200, 400)
        assert get_content_type(response) == YANG_XML

        # case multiple elem in list
        if response.status_code == 400:
            return

        root = ET.fromstring(response.text)
        assert root.tag == f"{{{RT_NS}}}allowed-ips", (
            f"la racine XML d'une leaf-list doit être "
            f"{{{RT_NS}}}allowed-ips, obtenu {root.tag!r}"
        )


# ============================================================================
# T-GET-07 : GET avec Accept: application/yang-data+json
# ============================================================================


@pytest.mark.roadmap("R41")
@pytest.mark.rfc("RFC 8040 §4.2")
@pytest.mark.rfc("RFC 7951")
class TestT_GET_07_AcceptJson:
    """
    T-GET-07 : GET avec ``Accept: application/yang-data+json`` → réponse JSON YANG.

    RFC 8040 §4.2 : le media type ``application/yang-data+json`` identifie
    l'encodage JSON des données YANG.
    """

    def test_accept_json_returns_json(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers)

        assert response.status_code == 200, (
            f"GET {BASIC_DATA} avec Accept JSON doit retourner 200, "
            f"obtenu {response.status_code}"
        )
        assert get_content_type(response) == YANG_JSON, (
            f"Content-Type attendu {YANG_JSON!r}, "
            f"obtenu {response.headers.get('content-type')!r}"
        )
        # Le corps doit être un JSON valide (lève ValueError sinon).
        body = response.json()
        assert_json_data_envelope(body, f"{MOD}:basic-data")


# ============================================================================
# T-GET-08 : GET avec Accept: application/yang-data+xml
# ============================================================================


@pytest.mark.roadmap("R41")
@pytest.mark.rfc("RFC 8040 §4.2")
@pytest.mark.rfc("RFC 7951")
class TestT_GET_08_AcceptXml:
    """
    T-GET-08 : GET avec ``Accept: application/yang-data+xml`` → réponse XML YANG.

    RFC 8040 §4.2 : le media type ``application/yang-data+xml`` identifie
    l'encodage XML des données YANG.
    """

    def test_accept_xml_returns_xml(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_XML, **auth_headers}
        response = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers)

        assert response.status_code == 200, (
            f"GET {BASIC_DATA} avec Accept XML doit retourner 200, "
            f"obtenu {response.status_code}"
        )
        assert get_content_type(response) == YANG_XML, (
            f"Content-Type attendu {YANG_XML!r}, "
            f"obtenu {response.headers.get('content-type')!r}"
        )
        # Le corps doit être un XML bien formé (lève ET.ParseError sinon).
        root = ET.fromstring(response.text)
        assert root.tag == f"{{{RT_NS}}}basic-data", (
            f"la racine XML doit être {{{RT_NS}}}basic-data, obtenu {root.tag!r}"
        )


# ============================================================================
# T-GET-09 : GET sur ressource inexistante
# ============================================================================


@pytest.mark.roadmap("R6")
@pytest.mark.rfc("RFC 8040 §4.3")
@pytest.mark.rfc("RFC 8040 §7")
class TestT_GET_09_NotFound:
    """
    T-GET-09 : GET sur ressource inexistante → 404 avec erreur RESTCONF.

    RFC 8040 §4.3 : une ressource de données inexistante est signalée par
    ``404 Not Found``.
    RFC 8040 §7 : l'erreur est renvoyée dans l'enveloppe
    ``ietf-restconf:errors`` (error-tag typiquement ``invalid-value`` ou
    ``data-missing``).
    """

    @pytest.mark.parametrize(
        "path",
        [
            "/data/restconf-test:does-not-exist",        # nœud inexistant (module connu)
            "/data/no-such-module:basic-data",           # module inconnu
            "/data/restconf-test:basic-data/no-such-leaf",  # leaf inexistante
        ],
        ids=["unknown-node", "unknown-module", "unknown-leaf"],
    )
    def test_get_nonexistent_returns_404(
        self, http2_client, api_url, auth_headers, require_rt, path
    ):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{path}", headers=headers)

        assert response.status_code == 404, (
            f"GET {api_url}{path} (ressource inexistante) doit retourner 404, "
            f"obtenu {response.status_code}"
        )

        # L'erreur doit être une enveloppe RESTCONF dans le media type demandé.
        content_type = get_content_type(response)
        if content_type == YANG_JSON and response.content:
            assert_json_error_envelope(response.json())
        elif content_type == YANG_XML and response.content:
            assert_xml_error_envelope(response.text)

    def test_404_error_envelope_xml(self, http2_client, api_url, auth_headers, require_rt):
        """Une erreur 404 demandée en XML utilise l'enveloppe XML RESTCONF."""
        headers = {"Accept": YANG_XML, **auth_headers}
        response = http2_client.get(
            f"{api_url}/data/restconf-test:does-not-exist", headers=headers
        )

        assert response.status_code == 404
        if get_content_type(response) == YANG_XML and response.content:
            assert_xml_error_envelope(response.text)


# ============================================================================
# T-GET-10 : GET avec chemin percent-encodé
# ============================================================================


@pytest.mark.roadmap("R43")
@pytest.mark.rfc("RFC 8040 §3.5.3")
@pytest.mark.rfc("RFC 3986 §2.1")
class TestT_GET_10_PercentEncodedPath:
    """
    T-GET-10 : GET avec chemin percent-encodé : décodage avant traitement.

    RFC 8040 §3.5.3 / RFC 3986 §2.1 : les séquences percent-encodées du
    chemin doivent être décodées avant l'interprétation de l'``api-path``.
    Le séparateur ``:`` de ``restconf-test:basic-data`` peut être encodé
    ``%3A`` ; les deux formes doivent être traitées de manière identique.
    """

    def test_percent_encoded_colon_equivalent(
        self, http2_client, api_url, auth_headers, require_rt
    ):
        """``restconf-test%3Abasic-data`` ≡ ``restconf-test:basic-data``."""
        headers = {"Accept": YANG_JSON, **auth_headers}

        plain = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers)
        encoded = http2_client.get(
            f"{api_url}/data/restconf-test%3Abasic-data", headers=headers
        )

        assert encoded.status_code == plain.status_code, (
            f"le chemin percent-encodé 'restconf-test%3Abasic-data' doit être "
            f"traité comme 'restconf-test:basic-data' : "
            f"plain={plain.status_code} encoded={encoded.status_code}"
        )
        if plain.status_code == 200:
            assert encoded.json() == plain.json(), (
                "les chemins encodé et non encodé doivent retourner le même corps"
            )

    def test_percent_encoded_leaf(self, http2_client, api_url, auth_headers, require_rt):
        """Décodage d'une leaf via un chemin percent-encodé."""
        headers = {"Accept": YANG_JSON, **auth_headers}

        plain = http2_client.get(
            f"{api_url}{BASIC_DATA}/device-id", headers=headers
        )
        encoded = http2_client.get(
            f"{api_url}/data/restconf-test%3Abasic-data/device-id", headers=headers
        )

        assert encoded.status_code == plain.status_code, (
            f"chemin percent-encodé mal décodé : plain={plain.status_code} "
            f"encoded={encoded.status_code}"
        )
        if plain.status_code == 200:
            assert encoded.json() == plain.json()


# ============================================================================
# T-GET-11 : GET avec clés de liste (virgules / caractères spéciaux)
# ============================================================================


@pytest.mark.roadmap("R43")
@pytest.mark.rfc("RFC 8040 §3.5.3")
@pytest.mark.rfc("RFC 3986 §2.1")
class TestT_GET_11_ListKeys:
    """
    T-GET-11 : GET avec clés de liste contenant virgules / caractères spéciaux.

    RFC 8040 §3.5.3 : dans un ``api-path``, les clés de liste sont exprimées
    ``list-name=key-value`` ; la virgule séparant les clés et les caractères
    réservés doivent être percent-encodés dans la valeur de clé, puis décodés
    avant la construction du XPath.

    Le module ``restconf-test`` déclare ``keyed-entries/keyed-entry`` dont la
    clé ``label`` est une chaîne libre. Le jeu de données contient deux clés
    spéciales : ``with,comma`` (virgule) et ``with space`` (espace). Elles
    doivent être encodées ``with%2Ccomma`` et ``with%20space`` sur le fil.
    """

    @pytest.mark.parametrize(
        "label,value",
        sorted(SPECIAL_KEYS.items()),
        ids=["space-key", "comma-key"],
    )
    def test_get_special_key(
        self, http2_client, api_url, auth_headers, require_rt, label, value
    ):
        """Une clé contenant ',' ou ' ' est décodée et résolue correctement."""
        # quote(safe="") encode ',' -> %2C et ' ' -> %20 (RFC 3986 §2.1).
        encoded_key = quote(label, safe="")
        path = f"{api_url}{KEYED_ENTRIES}/keyed-entry={encoded_key}"
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(path, headers=headers)

        assert response.status_code == 200, (
            f"GET keyed-entry={encoded_key} (clé {label!r} percent-encodée) "
            f"doit retourner 200 après décodage (RFC 8040 §3.5.3), "
            f"obtenu {response.status_code}"
        )
        assert get_content_type(response) == YANG_JSON

        body = response.json()
        assert_json_data_envelope(body, f"{MOD}:keyed-entry")
        entry = body[f"{MOD}:keyed-entry"]
        assert isinstance(entry, dict), (
            f"une instance de liste unique doit être un objet JSON, "
            f"obtenu {type(entry).__name__}"
        )
        # La clé décodée et la valeur associée (mêmes module → non préfixées).
        assert entry.get("label") == label, (
            f"la clé décodée doit être {label!r}, obtenu {entry.get('label')!r}"
        )
        assert entry.get("value") == value, (
            f"la valeur de keyed-entry={label!r} doit être {value!r}, "
            f"obtenu {entry.get('value')!r}"
        )

    def test_comma_key_not_split_into_multiple_keys(
        self, http2_client, api_url, auth_headers, require_rt
    ):
        """La virgule encodée ``%2C`` n'est pas interprétée comme un séparateur.

        Si le serveur décodait mal (ou ne décodait pas) la virgule, la clé
        ``with%2Ccomma`` serait soit introuvable (``404``), soit scindée en
        deux clés ``with`` et ``comma`` (erreur, ``keyed-entry`` n'ayant
        qu'une seule clé). Un ``200`` avec la bonne valeur prouve le décodage
        correct.
        """
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{KEYED_ENTRIES}/keyed-entry=with%2Ccomma", headers=headers
        )
        assert response.status_code == 200, (
            f"la virgule percent-encodée doit être décodée comme un caractère "
            f"de la clé, pas comme un séparateur ; obtenu {response.status_code}"
        )
        entry = response.json()[f"{MOD}:keyed-entry"]
        assert entry.get("label") == "with,comma"
        assert entry.get("value") == "key contains a comma"


@pytest.mark.roadmap("R43")
@pytest.mark.rfc("RFC 8040 §3.5.3")
@pytest.mark.rfc("RFC 3986 §2.1")
class TestT_GET_11_MultiKeyList:
    """
    T-GET-11 (clés multiples) : GET sur une liste à plusieurs clés.

    RFC 8040 §3.5.3, `Errata EID 5255
    <https://www.rfc-editor.org/errata/eid5255>`_ : pour une liste à
    plusieurs clés, le path segment doit être construit comme
    ``list-name=key1,key2,...`` — un seul ``=`` introduit le *jeu* de
    valeurs de clé, qui sont ensuite séparées par des virgules dans l'ordre
    de déclaration des clés dans le YANG (ici ``region label``). Le texte
    initial de la RFC omettait ce ``=`` pour le cas multi-clés.

    Le module ``restconf-test`` déclare ``keyed-entries/multi-keyed-entry``
    avec ``key "region label"``.
    """

    @pytest.mark.parametrize(
        "keys,value",
        sorted(
            ((k, v) for k, v in MULTI_KEYS.items() if "," not in k[1]),
            key=lambda kv: kv[0],
        ),
        ids=["eu-alpha", "us-beta"],
    )
    def test_get_multi_key_entry(
        self, http2_client, api_url, auth_headers, require_rt, keys, value
    ):
        """Une entrée à deux clés est résolue via ``list=key1,key2``."""
        region, label = keys
        path = f"{api_url}{MULTI_KEYED_ENTRIES}={quote(region, safe='')},{quote(label, safe='')}"
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(path, headers=headers)

        assert response.status_code == 200, (
            f"GET multi-keyed-entry={region},{label} doit retourner 200 "
            f"(RFC 8040 §3.5.3, Errata EID 5255), obtenu "
            f"{response.status_code} : {response.text[:300]}"
        )
        assert get_content_type(response) == YANG_JSON

        body = response.json()
        assert_json_data_envelope(body, f"{MOD}:multi-keyed-entry")
        entry = body[f"{MOD}:multi-keyed-entry"]
        assert isinstance(entry, dict), (
            f"une instance de liste unique doit être un objet JSON, "
            f"obtenu {type(entry).__name__}"
        )
        assert entry.get("region") == region, (
            f"la première clé décodée doit être {region!r}, "
            f"obtenu {entry.get('region')!r}"
        )
        assert entry.get("label") == label, (
            f"la seconde clé décodée doit être {label!r}, "
            f"obtenu {entry.get('label')!r}"
        )
        assert entry.get("value") == value, (
            f"la valeur de multi-keyed-entry={region},{label} doit être "
            f"{value!r}, obtenu {entry.get('value')!r}"
        )

    def test_encoded_comma_within_second_key_not_split(
        self, http2_client, api_url, auth_headers, require_rt
    ):
        """Une virgule percent-encodée *dans* la 2e clé n'ajoute pas de clé.

        La clé ``("eu", "with,comma")`` s'encode
        ``multi-keyed-entry=eu,with%2Ccomma`` : un seul ``=``, puis deux
        valeurs de clé séparées par une virgule *non* encodée, la virgule
        interne à la seconde valeur étant percent-encodée (``%2C``). Si le
        serveur décodait le ``%2C`` avant de découper les clés, la valeur
        serait scindée en trois clés (``eu``, ``with``, ``comma``) pour une
        liste qui n'en a que deux, et échouerait ; à l'inverse, décoder
        après découpage restitue correctement ``with,comma`` comme seconde
        clé.
        """
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{MULTI_KEYED_ENTRIES}=eu,with%2Ccomma", headers=headers
        )
        assert response.status_code == 200, (
            f"la virgule percent-encodée dans la seconde clé doit être "
            f"décodée comme un caractère de la clé, pas comme un séparateur "
            f"supplémentaire ; obtenu {response.status_code} : "
            f"{response.text[:300]}"
        )
        entry = response.json()[f"{MOD}:multi-keyed-entry"]
        assert entry.get("region") == "eu"
        assert entry.get("label") == "with,comma"
        assert entry.get("value") == "second key contains a comma"

    def test_missing_second_key_is_rejected(
        self, http2_client, api_url, auth_headers, require_rt
    ):
        """Un jeu de clés incomplet (une seule valeur) est rejeté.

        RFC 8040 §3.5.3 : le nombre de valeurs de clé dans l'api-path doit
        correspondre au nombre de clés déclarées dans le YANG. Fournir une
        seule valeur pour une liste à deux clés est structurellement
        invalide.
        """
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(
            f"{api_url}{MULTI_KEYED_ENTRIES}=eu", headers=headers
        )
        assert response.status_code in (400, 404), (
            f"un jeu de clés incomplet doit être rejeté (400 ou 404 selon "
            f"RFC 8040 §7), obtenu {response.status_code}"
        )


# ============================================================================
# T-GET-12 : GET avec chemin invalide
# ============================================================================


@pytest.mark.roadmap("R43")
@pytest.mark.rfc("RFC 8040 §3.5.3")
@pytest.mark.rfc("RFC 8040 §7")
class TestT_GET_12_InvalidPath:
    """
    T-GET-12 : GET avec chemin invalide → 400 ou 404 avec enveloppe d'erreur.

    RFC 8040 §3.5.3 : un ``api-path`` syntaxiquement invalide doit être
    rejeté.
    RFC 8040 §7 : l'erreur est renvoyée dans l'enveloppe
    ``ietf-restconf:errors`` (``400 Bad Request`` pour un chemin mal formé,
    ``404 Not Found`` selon le contexte).
    """

    @pytest.mark.parametrize(
        "path",
        [
            # Sous-ressource sous une leaf : structurellement invalide.
            "/data/restconf-test:basic-data/device-id/junk",
            # Segment de données vide / mal formé.
            "/data/restconf-test:basic-data//device-id",
            # Préfixe de module sans nom de nœud.
            "/data/restconf-test:",
        ],
        ids=["child-of-leaf", "empty-segment", "dangling-module-prefix"],
    )
    def test_invalid_path_rejected(
        self, http2_client, api_url, auth_headers, require_rt, path
    ):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{path}", headers=headers)

        assert response.status_code in (400, 404), (
            f"GET {api_url}{path} (chemin invalide) doit retourner 400 ou 404, "
            f"obtenu {response.status_code}"
        )
        # Pas d'erreur serveur sur un chemin invalide.
        assert response.status_code < 500

        content_type = get_content_type(response)
        if content_type == YANG_JSON and response.content:
            assert_json_error_envelope(response.json())
        elif content_type == YANG_XML and response.content:
            assert_xml_error_envelope(response.text)
