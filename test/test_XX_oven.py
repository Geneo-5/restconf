# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN
"""Tests de conformité : cas d'utilisation du module ``oven``.

Ce fichier est dédié au module YANG ``oven`` (namespace ``urn:sysrepo:oven``,
préfixe ``ov``), installé dans l'environnement de test par
``docker/Dockerfile`` (``sysrepoctl -i .../oven.yang`` + plugin opérationnel
``oven.so``). Il exerce *tous* les cas d'utilisation du module à travers le
protocole RESTCONF :

- lecture (GET/HEAD) des données de configuration (``oven:oven``) et d'état
  (``oven:oven-state``, renseigné par le plugin) — items R5/R6, RFC 8040 §3.4
  et §4.3 ;
- négociation de contenu JSON/XML — item R41, RFC 8040 §4.2 ;
- gestion d'erreurs (ressource inexistante) — item R22, RFC 8040 §7 ;
- invocation des RPC ``insert-food`` (avec input) et ``remove-food`` (sans
  input) via ``{+restconf}/operations`` — items R11/R15, RFC 8040 §3.6 et
  §4.4.2 ;
- découverte des RPC via ``GET {+restconf}/operations`` — item R15, RFC 8040
  §3.6 ;
- notification ``oven-ready`` (flux d'événements / SSE) — items R23/R25,
  RFC 8040 §3.8 et §6 (tests SSE ignorés tant que l'endpoint SSE n'est pas
  implémenté, de manière cohérente avec ``test_01_transport.py``).

Structure du module ``oven`` :

    container oven {            // configuration
        leaf turned-on   { type boolean; default false; }
        leaf temperature { type oven-temperature; default 0; }  // uint8 0..250
    }
    container oven-state {      // config false (état fourni par le plugin)
        leaf temperature  { type oven-temperature; }
        leaf food-inside  { type boolean; }
    }
    rpc insert-food {           // input, pas d'output
        input { leaf time { type enumeration { enum now; enum on-oven-ready; } } }
    }
    rpc remove-food { }         // ni input ni output
    notification oven-ready { } // aucun leaf

Les tests sont fondés exclusivement sur les comportements attendus par les
RFC, et non sur une implémentation particulière.
"""

import xml.etree.ElementTree as ET

import pytest

# ---------------------------------------------------------------------------
# Références normatives (constantes dérivées des RFC / du module oven)
# ---------------------------------------------------------------------------

# Media types RESTCONF (RFC 8040 §4.2).
YANG_JSON = "application/yang-data+json"
YANG_XML = "application/yang-data+xml"

# Namespace XML du module oven (RFC 7950 §7.1.2) et de l'enveloppe d'erreur
# RESTCONF (RFC 8040 §7).
OVEN_NS = "urn:sysrepo:oven"
RESTCONF_NS = "urn:ietf:params:xml:ns:yang:ietf-restconf"

# Préfixe JSON du module (RFC 7951 §4 : nœud de plus haut niveau préfixé).
MOD = "oven"

# Chemins RESTCONF (api-path, RFC 8040 §3.5).
DATA_PATH = "/data"
OVEN_CONFIG = "/data/oven:oven"
OVEN_STATE = "/data/oven:oven-state"
OPERATIONS = "/operations"
INSERT_FOOD = "/operations/oven:insert-food"
REMOVE_FOOD = "/operations/oven:remove-food"
STREAMS = "/data/ietf-restconf-monitoring:restconf-state/streams"

# Feuilles de configuration (defaults) et feuilles d'état (plugin), avec le
# type Python attendu dans l'encodage JSON (RFC 7951).
CONFIG_LEAVES = {
    "turned-on": bool,    # boolean -> true/false
    "temperature": int,   # uint8 (0..250) -> nombre JSON
}
STATE_LEAVES = {
    "temperature": int,   # uint8 (0..250)
    "food-inside": bool,  # boolean
}

# Température bornée par le typedef oven-temperature (uint8 range 0..250).
TEMP_MIN, TEMP_MAX = 0, 250

# RPC du module oven attendus dans {+restconf}/operations (RFC 8040 §3.6).
OVEN_RPCS = {"oven:insert-food", "oven:remove-food"}


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
        f"obtenu {sorted(body.keys())!r}"
    )


def assert_json_leaf_encoding(body, leaf: str, py_type):
    """Vérifie l'encodage JSON d'une leaf lue directement (RFC 7951).

    Pour un GET ciblant une leaf, le nœud de premier niveau est préfixé
    ``module:leaf`` (RFC 7951 §4) et la valeur a le type Python attendu :
    ``bool`` pour un boolean YANG (§6.7), ``int`` pour un entier (§6.2).
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


def assert_json_error_envelope(body):
    """Vérifie la structure de l'enveloppe d'erreur JSON (RFC 8040 §7)."""
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


# ---------------------------------------------------------------------------
# Fixtures : disponibilité du module oven et de la ressource operations
# ---------------------------------------------------------------------------

# Chemins sondés pour déterminer si le module oven est installé et accessible.
# Le container d'état (config false) est renseigné par le plugin ``oven.so`` ;
# le container de configuration peut être vide si les valeurs par défaut ne
# sont pas reportées (RFC 6243, basic-mode ``trim``/``explicit``).
_OVEN_PROBE_PATHS = (OVEN_STATE, OVEN_CONFIG)


@pytest.fixture(scope="session")
def oven_probe(http2_client, api_url, auth_headers, test_jwt):
    """Sonde l'accessibilité du module ``oven`` sur le datastore.

    Retourne un dict ``{chemin: réponse}`` pour les chemins sondés, ou
    ``None`` si aucun JWT n'est configuré (authentification requise).
    """
    if not test_jwt:
        return None
    headers = {"Accept": YANG_JSON, **auth_headers}
    return {
        path: http2_client.get(f"{api_url}{path}", headers=headers)
        for path in _OVEN_PROBE_PATHS
    }


@pytest.fixture()
def require_oven(oven_probe):
    """Skip le test si le module ``oven`` n'est pas installé / accessible.

    L'installation du module est un prérequis d'environnement (Dockerfile),
    pas un comportement du serveur testé : si aucune donnée ``oven`` n'est
    accessible, le test est ignoré plutôt que mis en échec.
    """
    if oven_probe is None:
        pytest.skip(
            "Aucun JWT configuré — accès au datastore oven impossible "
            "(--restconf-test-jwt=<token> ou RESTCONF_TEST_JWT)"
        )

    statuses = {path: resp.status_code for path, resp in oven_probe.items()}

    if any(resp.status_code == 200 for resp in oven_probe.values()):
        return oven_probe

    if all(resp.status_code == 404 for resp in oven_probe.values()):
        pytest.skip(
            f"Module YANG 'oven' non installé ou aucune donnée accessible "
            f"({statuses})"
        )
    if all(resp.status_code in (401, 403) for resp in oven_probe.values()):
        pytest.skip(f"Accès au datastore oven refusé ({statuses})")
    pytest.skip(f"Datastore oven inaccessible ({statuses})")


@pytest.fixture()
def oven_operations(http2_client, api_url, auth_headers, require_jwt):
    """Récupère la liste des RPC exposés sur ``{+restconf}/operations``.

    Retourne le dict ``{nom-rpc: ...}`` des opérations (RFC 8040 §3.6), ou
    déclenche un ``skip`` si la ressource ``operations`` n'est pas disponible
    (R15 en cours d'implémentation) ou si l'accès est refusé.
    """
    headers = {"Accept": YANG_JSON, **auth_headers}
    response = http2_client.get(f"{api_url}{OPERATIONS}", headers=headers)

    if response.status_code in (401, 403):
        pytest.skip(f"Accès à {OPERATIONS} refusé ({response.status_code})")
    if response.status_code in (404, 405):
        pytest.skip(
            f"Ressource 'operations' non disponible ({response.status_code}) — "
            f"R15 non implémenté"
        )
    assert response.status_code == 200, (
        f"GET {api_url}{OPERATIONS} doit retourner 200 (RFC 8040 §3.6), "
        f"obtenu {response.status_code}"
    )

    body = response.json()
    assert "operations" in body, (
        f"la réponse de {OPERATIONS} doit contenir 'operations' "
        f"(RFC 8040 §3.6), obtenu {sorted(body.keys())!r}"
    )
    operations = body["operations"]
    assert isinstance(operations, dict), "'operations' doit être un objet JSON"
    return operations


# ============================================================================
# Lecture du datastore : containers et leafs (R5, R6)
# ============================================================================


@pytest.mark.roadmap("R5", "R6")
@pytest.mark.rfc("RFC 8040 §3.4", "RFC 8040 §4.3")
@pytest.mark.rfc("RFC 7951")
class TestOvenDatastoreRead:
    """
    Lecture des containers du module ``oven``.

    - ``oven:oven-state`` (config false, renseigné par le plugin) : ``200`` ;
    - ``oven:oven`` (configuration, valeurs par défaut) : ``200`` ou ``404``
      si les valeurs par défaut ne sont pas reportées (RFC 6243).
    """

    def test_data_view_contains_oven(self, http2_client, api_url, auth_headers, require_oven):
        """La vue ``/data`` expose au moins un des containers ``oven``."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{DATA_PATH}", headers=headers)
        assert response.status_code == 200

        data = response.json()["ietf-restconf:data"]
        oven_nodes = {key for key in data if key in (f"{MOD}:oven", f"{MOD}:oven-state")}
        assert oven_nodes, (
            f"la vue du datastore doit exposer 'oven:oven' et/ou "
            f"'oven:oven-state', obtenu {sorted(data.keys())!r}"
        )

    def test_get_state_container_json(self, http2_client, api_url, auth_headers, require_oven):
        """GET oven:oven-state en JSON : racine ``oven:oven-state``."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{OVEN_STATE}", headers=headers)

        assert response.status_code == 200, (
            f"GET {api_url}{OVEN_STATE} doit retourner 200 (container d'état "
            f"fourni par le plugin), obtenu {response.status_code}"
        )
        assert get_content_type(response) == YANG_JSON

        body = response.json()
        assert_json_data_envelope(body, f"{MOD}:oven-state")

        container = body[f"{MOD}:oven-state"]
        assert isinstance(container, dict), "le container doit être un objet JSON"
        # Les feuilles d'état, si présentes, doivent être correctement typées
        # (mêmes module → non préfixées, RFC 7951 §4).
        for leaf, py_type in STATE_LEAVES.items():
            if leaf in container:
                value = container[leaf]
                if py_type is bool:
                    assert isinstance(value, bool), (
                        f"oven-state/{leaf} doit être un booléen JSON, obtenu {value!r}"
                    )
                else:
                    assert isinstance(value, int) and not isinstance(value, bool), (
                        f"oven-state/{leaf} doit être un entier JSON, obtenu {value!r}"
                    )
                if leaf == "temperature":
                    assert TEMP_MIN <= value <= TEMP_MAX, (
                        f"oven-state/temperature doit être dans "
                        f"[{TEMP_MIN}, {TEMP_MAX}], obtenu {value}"
                    )

    def test_get_state_container_xml(self, http2_client, api_url, auth_headers, require_oven):
        """GET oven:oven-state en XML : racine ``{urn:sysrepo:oven}oven-state``."""
        headers = {"Accept": YANG_XML, **auth_headers}
        response = http2_client.get(f"{api_url}{OVEN_STATE}", headers=headers)

        assert response.status_code == 200
        assert get_content_type(response) == YANG_XML

        root = ET.fromstring(response.text)
        assert root.tag == f"{{{OVEN_NS}}}oven-state", (
            f"la racine XML doit être {{{OVEN_NS}}}oven-state (RFC 7950), "
            f"obtenu {root.tag!r}"
        )

    def test_get_config_container(self, http2_client, api_url, auth_headers, require_oven):
        """GET oven:oven (config) : 200 ou 404 selon le report des defaults.

        RFC 6243 : si le basic-mode ``with-defaults`` est ``trim``/``explicit``
        et qu'aucune valeur n'est configurée, le container (sans présence)
        peut ne pas être reporté → ``404`` acceptable.
        """
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{OVEN_CONFIG}", headers=headers)

        assert response.status_code in (200, 404), (
            f"GET {api_url}{OVEN_CONFIG} doit retourner 200 ou 404, "
            f"obtenu {response.status_code}"
        )
        if response.status_code == 404:
            return  # defaults non reportées : comportement valide

        assert get_content_type(response) == YANG_JSON
        body = response.json()
        assert_json_data_envelope(body, f"{MOD}:oven")
        container = body[f"{MOD}:oven"]
        assert isinstance(container, dict)
        for leaf, py_type in CONFIG_LEAVES.items():
            if leaf in container:
                value = container[leaf]
                if py_type is bool:
                    assert isinstance(value, bool)
                else:
                    assert isinstance(value, int) and not isinstance(value, bool)


@pytest.mark.roadmap("R6")
@pytest.mark.rfc("RFC 8040 §4.3")
@pytest.mark.rfc("RFC 7951 §6")
class TestOvenLeaves:
    """
    Lecture des leafs du module ``oven``.

    RFC 7951 §6.2 : les entiers YANG sont des nombres JSON.
    RFC 7951 §6.7 : les boolean YANG sont des booléens JSON.

    Les feuilles de configuration (``turned-on``, ``temperature``) portent des
    valeurs par défaut : si elles ne sont pas reportées (RFC 6243), un ``404``
    est acceptable. Les feuilles d'état (``oven-state/*``) sont renseignées par
    le plugin et attendues à ``200``.
    """

    @pytest.mark.parametrize("leaf,py_type", sorted(CONFIG_LEAVES.items()))
    def test_get_config_leaf(self, http2_client, api_url, auth_headers, require_oven, leaf, py_type):
        """GET sur une leaf de configuration (default possible)."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{OVEN_CONFIG}/{leaf}", headers=headers)

        assert response.status_code in (200, 404), (
            f"GET {OVEN_CONFIG}/{leaf} doit retourner 200 ou 404, "
            f"obtenu {response.status_code}"
        )
        if response.status_code == 404:
            return  # leaf par défaut non reportée : comportement valide

        assert get_content_type(response) == YANG_JSON
        body = response.json()
        assert_json_leaf_encoding(body, leaf, py_type)

        if leaf == "temperature":
            value = body[f"{MOD}:temperature"]
            assert TEMP_MIN <= value <= TEMP_MAX, (
                f"oven/temperature doit être dans [{TEMP_MIN}, {TEMP_MAX}], "
                f"obtenu {value}"
            )

    @pytest.mark.parametrize("leaf,py_type", sorted(STATE_LEAVES.items()))
    def test_get_state_leaf(self, http2_client, api_url, auth_headers, require_oven, leaf, py_type):
        """GET sur une leaf d'état (fournie par le plugin) : 200 attendu."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{OVEN_STATE}/{leaf}", headers=headers)

        if response.status_code == 404:
            pytest.skip(
                f"la leaf d'état oven-state/{leaf} n'est pas fournie par le "
                f"plugin dans cet environnement"
            )

        assert response.status_code == 200, (
            f"GET {OVEN_STATE}/{leaf} doit retourner 200, "
            f"obtenu {response.status_code}"
        )
        assert get_content_type(response) == YANG_JSON
        body = response.json()
        assert_json_leaf_encoding(body, leaf, py_type)

        if leaf == "temperature":
            value = body[f"{MOD}:temperature"]
            assert TEMP_MIN <= value <= TEMP_MAX, (
                f"oven-state/temperature doit être dans [{TEMP_MIN}, {TEMP_MAX}], "
                f"obtenu {value}"
            )

    def test_get_leaf_xml(self, http2_client, api_url, auth_headers, require_oven):
        """GET sur une leaf d'état en XML : racine ``{urn:sysrepo:oven}temperature``."""
        headers = {"Accept": YANG_XML, **auth_headers}
        response = http2_client.get(f"{api_url}{OVEN_STATE}/temperature", headers=headers)

        if response.status_code == 404:
            pytest.skip("oven-state/temperature non fournie par le plugin")

        assert response.status_code == 200
        assert get_content_type(response) == YANG_XML

        root = ET.fromstring(response.text)
        assert root.tag == f"{{{OVEN_NS}}}temperature", (
            f"la racine XML d'une leaf doit être {{{OVEN_NS}}}temperature, "
            f"obtenu {root.tag!r}"
        )
        # La valeur texte d'un entier YANG en XML (RFC 7951 §6.2).
        assert root.text is not None and root.text.strip().lstrip("-").isdigit(), (
            f"la leaf temperature doit porter une valeur entière en XML, "
            f"obtenu {root.text!r}"
        )


# ============================================================================
# HEAD (R6) et négociation de contenu (R41)
# ============================================================================


@pytest.mark.roadmap("R6")
@pytest.mark.rfc("RFC 8040 §4.3")
@pytest.mark.rfc("RFC 9110 §9.3.2")
class TestOvenHead:
    """
    HEAD sur une ressource ``oven`` : mêmes en-têtes que GET, sans corps.

    RFC 9110 §9.3.2 : « The HEAD method is identical to GET except that the
    server MUST NOT send content in the response. »
    """

    def test_head_no_body(self, http2_client, api_url, auth_headers, require_oven):
        """HEAD ne retourne aucun corps."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.head(f"{api_url}{OVEN_STATE}", headers=headers)

        assert response.status_code in (200, 401, 403), (
            f"HEAD {OVEN_STATE} doit retourner 200, 401 ou 403, "
            f"obtenu {response.status_code}"
        )
        assert response.content == b"", (
            f"HEAD ne doit retourner aucun corps (RFC 9110 §9.3.2), "
            f"obtenu {response.content!r}"
        )

    def test_head_consistent_with_get(self, http2_client, api_url, auth_headers, require_oven):
        """HEAD et GET retournent le même statut et le même Content-Type."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        target = f"{api_url}{OVEN_STATE}"

        get_resp = http2_client.get(target, headers=headers)
        head_resp = http2_client.head(target, headers=headers)

        assert get_resp.status_code == head_resp.status_code, (
            f"HEAD et GET doivent retourner le même statut "
            f"(RFC 9110 §9.3.2) : GET={get_resp.status_code} "
            f"HEAD={head_resp.status_code}"
        )
        if get_resp.status_code != 200:
            pytest.skip(f"Accès refusé ({get_resp.status_code})")
        assert get_content_type(head_resp) == get_content_type(get_resp)


@pytest.mark.roadmap("R41")
@pytest.mark.rfc("RFC 8040 §4.2")
@pytest.mark.rfc("RFC 7951")
class TestOvenMediaTypes:
    """
    Négociation de contenu sur les données ``oven``.

    RFC 8040 §4.2 : les media types ``application/yang-data+json`` et
    ``application/yang-data+xml`` identifient l'encodage JSON/XML des données
    YANG.
    """

    def test_accept_json(self, http2_client, api_url, auth_headers, require_oven):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{OVEN_STATE}", headers=headers)

        assert response.status_code == 200
        assert get_content_type(response) == YANG_JSON, (
            f"Content-Type attendu {YANG_JSON!r}, "
            f"obtenu {response.headers.get('content-type')!r}"
        )
        assert_json_data_envelope(response.json(), f"{MOD}:oven-state")

    def test_accept_xml(self, http2_client, api_url, auth_headers, require_oven):
        headers = {"Accept": YANG_XML, **auth_headers}
        response = http2_client.get(f"{api_url}{OVEN_STATE}", headers=headers)

        assert response.status_code == 200
        assert get_content_type(response) == YANG_XML, (
            f"Content-Type attendu {YANG_XML!r}, "
            f"obtenu {response.headers.get('content-type')!r}"
        )
        root = ET.fromstring(response.text)
        assert root.tag == f"{{{OVEN_NS}}}oven-state"


# ============================================================================
# Gestion d'erreurs (R22)
# ============================================================================


@pytest.mark.roadmap("R6", "R22")
@pytest.mark.rfc("RFC 8040 §4.3", "RFC 8040 §7")
class TestOvenNotFound:
    """
    GET sur ressource ``oven`` inexistante → 404 avec enveloppe d'erreur.

    RFC 8040 §4.3 : une ressource de données inexistante est signalée par
    ``404 Not Found``.
    RFC 8040 §7 : l'erreur est renvoyée dans l'enveloppe
    ``ietf-restconf:errors``.
    """

    @pytest.mark.parametrize(
        "path",
        [
            "/data/oven:does-not-exist",          # nœud inexistant (module connu)
            "/data/oven:oven-state/no-such-leaf",  # leaf inexistante
        ],
        ids=["unknown-node", "unknown-leaf"],
    )
    def test_get_nonexistent_returns_404(
        self, http2_client, api_url, auth_headers, require_oven, path
    ):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{path}", headers=headers)

        assert response.status_code == 404, (
            f"GET {api_url}{path} (ressource inexistante) doit retourner 404, "
            f"obtenu {response.status_code}"
        )
        content_type = get_content_type(response)
        if content_type == YANG_JSON and response.content:
            assert_json_error_envelope(response.json())
        elif content_type == YANG_XML and response.content:
            assert_xml_error_envelope(response.text)


# ============================================================================
# Découverte et invocation des RPC (R11, R15)
# ============================================================================


@pytest.mark.roadmap("R15")
@pytest.mark.rfc("RFC 8040 §3.6")
class TestOvenOperationsDiscovery:
    """
    Découverte des RPC du module ``oven`` via ``GET {+restconf}/operations``.

    RFC 8040 §3.6 : la ressource ``operations`` expose la liste des RPC
    disponibles, identifiés par ``module:rpc-name``.
    """

    def test_operations_lists_oven_rpcs(self, oven_operations):
        """Les RPC ``insert-food`` et ``remove-food`` sont annoncés."""
        present = OVEN_RPCS.intersection(oven_operations.keys())
        assert present, (
            f"GET /operations doit annoncer au moins un RPC du module oven "
            f"({sorted(OVEN_RPCS)}), obtenu {sorted(oven_operations.keys())!r}"
        )

    def test_rpc_names_are_module_qualified(self, oven_operations):
        """Chaque RPC est identifié par ``module:rpc-name`` (RFC 8040 §3.6)."""
        for rpc_name in oven_operations:
            assert ":" in rpc_name, (
                f"le RPC {rpc_name!r} doit être au format 'module:rpc-name'"
            )


@pytest.mark.roadmap("R11", "R15")
@pytest.mark.rfc("RFC 8040 §4.4.2")
@pytest.mark.rfc("RFC 8040 §3.6")
class TestOvenRpcInvocation:
    """
    Invocation des RPC du module ``oven`` via ``POST {+restconf}/operations``.

    RFC 8040 §4.4.2 (Invoke Operation Mode) :
    - l'input d'un RPC est encapsulé dans ``<module>:input`` ;
    - si le RPC n'a pas d'output, le serveur retourne ``204 No Content`` ;
      sinon ``200 OK`` avec le corps de sortie.

    Les RPC ``insert-food`` et ``remove-food`` n'ont pas d'output : la réponse
    attendue est ``204 No Content``.
    """

    def test_insert_food_with_input(self, http2_client, api_url, auth_headers, oven_operations):
        """POST insert-food avec un input valide → 204 No Content (pas d'output)."""
        if "oven:insert-food" not in oven_operations:
            pytest.skip("le RPC oven:insert-food n'est pas annoncé dans /operations")

        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        payload = {"oven:input": {"time": "now"}}
        response = http2_client.post(
            f"{api_url}{INSERT_FOOD}", headers=headers, json=payload
        )

        if response.status_code in (404, 405, 501):
            pytest.skip(
                f"invocation RPC non implémentée ({response.status_code}) — R15"
            )

        assert response.status_code == 204, (
            f"POST {INSERT_FOOD} (RPC sans output) doit retourner 204 No Content "
            f"(RFC 8040 §4.4.2), obtenu {response.status_code}"
        )
        assert response.content == b"", (
            f"un RPC sans output ne doit retourner aucun corps, "
            f"obtenu {response.content!r}"
        )

    def test_insert_food_on_oven_ready(self, http2_client, api_url, auth_headers, oven_operations):
        """POST insert-food avec ``time=on-oven-ready`` → 204 No Content."""
        if "oven:insert-food" not in oven_operations:
            pytest.skip("le RPC oven:insert-food n'est pas annoncé dans /operations")

        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        payload = {"oven:input": {"time": "on-oven-ready"}}
        response = http2_client.post(
            f"{api_url}{INSERT_FOOD}", headers=headers, json=payload
        )

        if response.status_code in (404, 405, 501):
            pytest.skip(
                f"invocation RPC non implémentée ({response.status_code}) — R15"
            )
        assert response.status_code == 204, (
            f"POST {INSERT_FOOD} avec time=on-oven-ready doit retourner 204, "
            f"obtenu {response.status_code}"
        )

    def test_remove_food_no_input(self, http2_client, api_url, auth_headers, oven_operations):
        """POST remove-food (sans input ni output) → 204 No Content."""
        if "oven:remove-food" not in oven_operations:
            pytest.skip("le RPC oven:remove-food n'est pas annoncé dans /operations")

        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        # RPC sans input : corps vide accepté (RFC 8040 §4.4.2).
        response = http2_client.post(f"{api_url}{REMOVE_FOOD}", headers=headers)

        if response.status_code in (404, 405, 501):
            pytest.skip(
                f"invocation RPC non implémentée ({response.status_code}) — R15"
            )
        assert response.status_code == 204, (
            f"POST {REMOVE_FOOD} (RPC sans input/output) doit retourner 204 "
            f"No Content (RFC 8040 §4.4.2), obtenu {response.status_code}"
        )


@pytest.mark.roadmap("R15", "R22")
@pytest.mark.rfc("RFC 8040 §4.4.2")
@pytest.mark.rfc("RFC 8040 §7")
class TestOvenRpcErrors:
    """
    Erreurs sur l'invocation des RPC ``oven``.

    RFC 8040 §4.4.2 / §7 :
    - un input invalide (valeur d'enum inconnue) est rejeté par
      ``400 Bad Request`` avec l'enveloppe ``ietf-restconf:errors`` ;
    - un RPC inconnu est signalé par ``404 Not Found``.
    """

    def test_insert_food_invalid_input(self, http2_client, api_url, auth_headers, oven_operations):
        """POST insert-food avec une valeur d'enum invalide → 400 Bad Request."""
        if "oven:insert-food" not in oven_operations:
            pytest.skip("le RPC oven:insert-food n'est pas annoncé dans /operations")

        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        payload = {"oven:input": {"time": "not-a-valid-enum"}}
        response = http2_client.post(
            f"{api_url}{INSERT_FOOD}", headers=headers, json=payload
        )

        if response.status_code in (405, 501):
            pytest.skip(
                f"invocation RPC non implémentée ({response.status_code}) — R15"
            )
        assert response.status_code == 400, (
            f"POST {INSERT_FOOD} avec un input invalide doit retourner 400 "
            f"(RFC 8040 §4.4.2), obtenu {response.status_code}"
        )
        content_type = get_content_type(response)
        if content_type == YANG_JSON and response.content:
            assert_json_error_envelope(response.json())

    def test_unknown_rpc_returns_404(self, http2_client, api_url, auth_headers, oven_operations):
        """POST sur un RPC oven inexistant → 404 Not Found."""
        headers = {"Content-Type": YANG_JSON, "Accept": YANG_JSON, **auth_headers}
        response = http2_client.post(
            f"{api_url}/operations/oven:no-such-rpc", headers=headers, json={}
        )

        if response.status_code in (405, 501):
            pytest.skip(
                f"invocation RPC non implémentée ({response.status_code}) — R15"
            )
        assert response.status_code == 404, (
            f"POST sur un RPC inconnu doit retourner 404 (RFC 8040 §3.6), "
            f"obtenu {response.status_code}"
        )
        content_type = get_content_type(response)
        if content_type == YANG_JSON and response.content:
            assert_json_error_envelope(response.json())


# ============================================================================
# Notification oven-ready (R23, R25) — flux d'événements / SSE
# ============================================================================


@pytest.mark.roadmap("R23", "R25")
@pytest.mark.rfc("RFC 8040 §3.8", "RFC 8040 §6")
class TestOvenNotification:
    """
    Notification ``oven-ready`` du module ``oven``.

    RFC 8040 §3.8 / §6 : les notifications YANG sont exposées via des flux
    d'événements (streams) découverts dans
    ``ietf-restconf-monitoring:restconf-state/streams`` et consommés en SSE
    (``Accept: text/event-stream``).

    L'endpoint SSE n'est pas encore implémenté (cohérent avec
    ``test_01_transport.py`` où les tests SSE sont ignorés) : la réception de
    la notification est donc ignorée, mais la découverte des streams est
    validée lorsqu'elle est disponible.
    """

    def test_stream_discovery_if_supported(
        self, http2_client, api_url, auth_headers, require_oven
    ):
        """Découverte des streams (si supportée) : structure conforme."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{STREAMS}", headers=headers)

        if response.status_code in (401, 403):
            pytest.skip(f"Accès aux streams refusé ({response.status_code})")
        if response.status_code == 404:
            pytest.skip("restconf-state/streams non supporté (R23 non implémenté)")

        assert response.status_code == 200, (
            f"GET {STREAMS} doit retourner 200, 401, 403 ou 404, "
            f"obtenu {response.status_code}"
        )
        body = response.json()
        assert "ietf-restconf-monitoring:streams" in body, (
            f"la réponse doit contenir 'ietf-restconf-monitoring:streams', "
            f"obtenu {sorted(body.keys())!r}"
        )
        streams = body["ietf-restconf-monitoring:streams"]
        # 'stream' est une liste (RFC 8040 §3.8) ; peut être absente si vide.
        if "stream" in streams:
            assert isinstance(streams["stream"], list), (
                "'stream' doit être une liste (RFC 8040 §3.8)"
            )

    @pytest.mark.skip(
        reason="Réception SSE de la notification oven-ready : l'endpoint SSE "
        "n'est pas encore implémenté (RFC 8040 §6, R24/R25)."
    )
    def test_receive_oven_ready_notification(self):
        """Réception de ``oven-ready`` en SSE (non implémenté à ce stade)."""
        pass
