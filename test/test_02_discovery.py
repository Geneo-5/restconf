# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN
"""Tests de conformité : découverte de la racine RESTCONF (section 2).

Couvre la section « 2. Découverte de la racine RESTCONF » de ``ROADMAP.md``
(item R3), soit les tests ``T-DISC-01`` à ``T-DISC-05``.

Ces tests sont fondés exclusivement sur les comportements attendus par les
RFC, et non sur une implémentation particulière :

- RFC 8040 §3.1 — *Root Resource Discovery* :
    « a RESTCONF client MUST determine the root of the RESTCONF API.
    There MUST be exactly one "restconf" link relation returned by the
    device. » Le client obtient ``/.well-known/host-meta`` ([RFC6415]) et
    utilise l'élément ``<Link>`` de relation ``restconf`` ; son attribut
    ``href`` identifie la racine ``{+restconf}``.

- RFC 6415 — *Web Host Metadata* :
    * §2  : le document host-meta est obtenu par un ``GET`` non authentifié
            sur ``/.well-known/host-meta`` ;
    * §3  : « the server MUST respond using the REQUIRED XRD 1.0 XML
            representation » lorsque le client demande ``application/xrd+xml``
            (ou n'envoie pas d'``Accept``) ; le media type ``application/xrd+xml``
            est le format REQUIRED ;
    * §3.1: « The host-meta document root MUST be an "XRD" element » ;
    * App. A (JRD) : la représentation JSON (``host-meta.json``) est
            RECOMMANDÉE mais optionnelle ; si elle n'est pas supportée le
            serveur « MUST respond with an HTTP 404 (Not Found) » ; lorsqu'elle
            est servie, le ``Content-Type`` « MUST » être ``application/json``
            et le document est un objet JSON dont les clés sont ``subject``,
            ``expires``, ``aliases``, ``properties`` et ``links``.

- RFC 9110 — *HTTP Semantics* (sémantique transverse) :
    * §11.6.1 : une réponse ``401 Unauthorized`` doit s'accompagner d'un
            en-tête ``WWW-Authenticate``.

La découverte de la racine est, par définition, un endpoint public : aucune
authentification n'est requise (RFC 8040 §3.1, RFC 6415 §2).
"""

import json
import xml.etree.ElementTree as ET
from urllib.parse import urlsplit

import pytest


# ---------------------------------------------------------------------------
# Références normatives (constantes dérivées des RFC)
# ---------------------------------------------------------------------------

# Well-known URIs enregistrées par la RFC 6415 (§6.1 et §6.2).
HOST_META = "/.well-known/host-meta"
HOST_META_JSON = "/.well-known/host-meta.json"

# Espace de noms XRD 1.0 (OASIS XRD 1.0, référencé par RFC 6415 §3).
XRD_NS = "http://docs.oasis-open.org/ns/xri/xrd-1.0"

# Media types normatifs.
XRD_CONTENT_TYPE = "application/xrd+xml"   # RFC 6415 §3 (format REQUIRED)
JSON_CONTENT_TYPE = "application/json"     # RFC 6415 App. A (JRD)

# En-têtes Accept conformes aux exemples de la RFC 8040 §3.1 / RFC 6415 §3.
ACCEPT_XRD = {"Accept": XRD_CONTENT_TYPE}

# Vocabulaire XRD 1.0 autorisé dans un document host-meta (RFC 6415 §3 :
# « Documents MAY include any elements included in the XRD 1.0 schema »).
XRD_ELEMENTS = {
    "Subject",
    "Alias",
    "Expires",
    "Property",
    "Properties",
    "Link",
    "Title",
}

# Clés de premier niveau d'un document JRD (RFC 6415 App. A).
JRD_KEYS = {"subject", "expires", "aliases", "properties", "links"}

# Mots-clés ne devant jamais apparaître dans une réponse de découverte, qui
# ne doit exposer que des métadonnées d'hôte (liens/propriétés) et aucune
# donnée de configuration ou d'état (T-DISC-05, RFC 6415 §5).
SENSITIVE_KEYWORDS = (
    "password",
    "passwd",
    "secret",
    "token",
    "authorization",
    "private",
    "credential",
    "sysrepo",
    "datastore",
    "jwt",
    "jwks",
    "api_key",
    "apikey",
    "session",
    "internal",
    "stack",
    "traceback",
    "core dump",
)


# ---------------------------------------------------------------------------
# Utilitaires de parsing et d'assertion (fondés sur les RFC)
# ---------------------------------------------------------------------------

def get_content_type(response) -> str:
    """Retourne le Content-Type de la réponse, sans ses paramètres (charset…)."""
    return response.headers.get("content-type", "").split(";")[0].strip().lower()


def parse_xrd(body: str):
    """Parse un document XRD (RFC 6415 §3) et retourne ``(racine, liens)``.

    ``liens`` est la liste des dictionnaires d'attributs de chaque élément
    ``{XRD_NS}Link``. Lève ``ET.ParseError`` si le corps n'est pas du XML
    bien formé.
    """
    root = ET.fromstring(body)
    links = [link.attrib for link in root.findall(f"{{{XRD_NS}}}Link")]
    return root, links


def restconf_links(links):
    """Retourne la liste des liens (attributs) de relation ``restconf``."""
    return [attrib for attrib in links if attrib.get("rel") == "restconf"]


def restconf_href(links):
    """Retourne le ``href`` du premier lien ``restconf``, ou ``None``."""
    for attrib in restconf_links(links):
        if attrib.get("href"):
            return attrib["href"]
    return None


def parse_json_links(body: str):
    """Parse la variante JRD (RFC 6415 App. A) et retourne ``(data, links)``."""
    data = json.loads(body)
    return data, data.get("links", [])


def normalize_path(path: str) -> str:
    """Normalise un chemin : supprime les '/' de fin, préserve la racine '/'."""
    return path.rstrip("/") or "/"


def href_to_path(href: str) -> str:
    """Extrait le chemin d'un ``href`` (URI absolue RFC 3986 ou chemin relatif).

    RFC 6415 §3.1.1 exprime les cibles de liens comme des URI ; RFC 8040 §3.1
    montre des ``href`` sous forme de chemins absolus (``/restconf``,
    ``/top/restconf``). On compare donc la composante *path* de l'URI.
    """
    parts = urlsplit(href)
    return normalize_path(parts.path) if parts.path else href


def assert_no_sensitive_text(value: str):
    """Vérifie qu'une valeur textuelle ne contient aucune donnée sensible."""
    if not value:
        return
    lowered = value.lower()
    for keyword in SENSITIVE_KEYWORDS:
        assert keyword not in lowered, (
            f"donnée potentiellement sensible {keyword!r} présente dans la "
            f"réponse de découverte : {value!r}"
        )


def assert_no_sensitive_body(body: str):
    """Vérifie l'absence de toute donnée sensible dans un corps brut."""
    assert_no_sensitive_text(body)


def iter_xrd_values(root):
    """Génère toutes les valeurs textuelles d'un document XRD (attributs + textes)."""
    for element in root.iter():
        for value in element.attrib.values():
            yield value
        if element.text:
            yield element.text


def iter_json_leaves(node):
    """Génère toutes les valeurs feuilles (scalaires) d'une structure JSON."""
    if isinstance(node, dict):
        for value in node.values():
            yield from iter_json_leaves(value)
    elif isinstance(node, list):
        for value in node:
            yield from iter_json_leaves(value)
    else:
        yield node


def assert_href_is_plain_root(href: str):
    """Vérifie que le href de la racine est un chemin simple (RFC 8040 §3.1).

    La racine ``{+restconf}`` est un chemin d'API ; son ``href`` ne doit
    véhiculer aucune donnée : ni query string, ni fragment.
    """
    parts = urlsplit(href)
    assert not parts.query, (
        f"le href de la racine {href!r} ne doit pas contenir de query string"
    )
    assert not parts.fragment, (
        f"le href de la racine {href!r} ne doit pas contenir de fragment"
    )


def get_host_meta_json(client):
    """Récupère ``host-meta.json`` ; ``skip`` si le serveur ne le supporte pas.

    RFC 6415 App. A : la représentation JRD est optionnelle ; si elle n'est
    pas servie à cet emplacement, le serveur « MUST respond with an HTTP 404 ».
    """
    response = client.get(HOST_META_JSON)
    assert response.status_code in (200, 404), (
        f"GET {HOST_META_JSON} doit renvoyer 200 (supporté) ou 404 "
        f"(non supporté, RFC 6415 App. A), obtenu {response.status_code}"
    )
    if response.status_code == 404:
        pytest.skip(
            "host-meta.json non supporté : représentation JRD optionnelle "
            "(RFC 6415 App. A / RFC 8040 §3.1)"
        )
    return response


# ---------------------------------------------------------------------------
# T-DISC-01 — GET non authentifié sur /.well-known/host-meta
# ---------------------------------------------------------------------------

@pytest.mark.roadmap("R3")
@pytest.mark.rfc("RFC 8040 §3.1")
@pytest.mark.rfc("RFC 6415 §2")
@pytest.mark.rfc("RFC 6415 §3")
def test_disc_01_unauthenticated_host_meta(client):
    """T-DISC-01 : réponse valide contenant un lien de relation ``restconf``.

    Comportement attendu (RFC 8040 §3.1 + RFC 6415) :

    - l'endpoint est public : la requête est émise *sans* en-tête
      ``Authorization`` ;
    - le serveur répond ``200 OK`` ;
    - la représentation est le format XRD 1.0 REQUIRED, servi avec le media
      type ``application/xrd+xml`` (RFC 6415 §3) ;
    - le document est un XML bien formé dont la racine est ``XRD`` (RFC 6415
      §3.1) ;
    - il contient un élément ``Link`` de relation ``restconf`` portant un
      attribut ``href`` (RFC 8040 §3.1).
    """
    response = client.get(HOST_META, headers=ACCEPT_XRD)

    assert response.status_code == 200, (
        f"GET {HOST_META} sans authentification doit renvoyer 200, "
        f"obtenu {response.status_code}"
    )
    assert get_content_type(response) == XRD_CONTENT_TYPE, (
        f"Content-Type attendu {XRD_CONTENT_TYPE!r} (RFC 6415 §3), "
        f"obtenu {response.headers.get('content-type')!r}"
    )

    root, links = parse_xrd(response.text)
    assert root.tag == f"{{{XRD_NS}}}XRD", (
        f"la racine du document doit être l'élément XRD (RFC 6415 §3.1), "
        f"obtenu {root.tag!r}"
    )

    href = restconf_href(links)
    assert href is not None, (
        f"aucun lien de relation 'restconf' avec attribut href dans la "
        f"réponse XRD (RFC 8040 §3.1) : {response.text!r}"
    )


# ---------------------------------------------------------------------------
# T-DISC-02 — Vérification du lien restconf
# ---------------------------------------------------------------------------

@pytest.mark.roadmap("R3")
@pytest.mark.rfc("RFC 8040 §3.1")
def test_disc_02_restconf_link_target(client, restconf_root):
    """T-DISC-02 : le lien pointe vers la racine RESTCONF correcte.

    RFC 8040 §3.1 : le ``href`` du lien ``restconf`` identifie la racine
    ``{+restconf}`` ; « the client MUST use this value as the initial part of
    the path in the request URI ».
    """
    response = client.get(HOST_META, headers=ACCEPT_XRD)
    assert response.status_code == 200

    _, links = parse_xrd(response.text)
    href = restconf_href(links)
    assert href is not None, "lien 'restconf' absent de host-meta"

    assert_href_is_plain_root(href)
    assert href_to_path(href) == restconf_root, (
        f"le lien 'restconf' doit pointer vers la racine {restconf_root!r}, "
        f"obtenu {href!r}"
    )


@pytest.mark.roadmap("R3")
@pytest.mark.rfc("RFC 8040 §3.1")
def test_disc_02_exactly_one_restconf_link(client):
    """T-DISC-02 : exactement une relation ``restconf`` est retournée.

    RFC 8040 §3.1 : « There MUST be exactly one "restconf" link relation
    returned by the device. » (un serveur peut annoncer d'autres relations,
    par ex. ``restconf2`` pour une version future — RFC 8040 §1.5 — mais une
    seule relation ``restconf`` exactement).
    """
    response = client.get(HOST_META, headers=ACCEPT_XRD)
    assert response.status_code == 200

    _, links = parse_xrd(response.text)
    count = len(restconf_links(links))
    assert count == 1, (
        f"exactement un lien de relation 'restconf' est requis (RFC 8040 "
        f"§3.1), obtenu {count} : {links!r}"
    )


@pytest.mark.roadmap("R3")
@pytest.mark.rfc("RFC 8040 §3.1")
@pytest.mark.rfc("RFC 6415 App. A")
def test_disc_02_link_consistent_between_xml_and_json(client, restconf_root):
    """T-DISC-02 (complément) : cohérence de la racine entre XRD et JRD.

    Les deux variantes décrivent la même ressource d'hôte (RFC 6415 App. A :
    le JRD est une représentation du XRD) et doivent donc annoncer la même
    racine RESTCONF (RFC 8040 §3.1). Testé uniquement si le serveur supporte
    ``host-meta.json`` (optionnel).
    """
    xml_resp = client.get(HOST_META, headers=ACCEPT_XRD)
    assert xml_resp.status_code == 200
    _, xml_links = parse_xrd(xml_resp.text)

    json_resp = get_host_meta_json(client)  # skip si 404
    _, json_links = parse_json_links(json_resp.text)

    xml_href = restconf_href(xml_links)
    json_href = restconf_href(json_links)

    assert xml_href is not None and json_href is not None
    assert href_to_path(xml_href) == href_to_path(json_href) == restconf_root, (
        f"les variantes XRD et JRD doivent annoncer la même racine "
        f"{restconf_root!r} ; obtenu XRD={xml_href!r} JRD={json_href!r}"
    )


# ---------------------------------------------------------------------------
# T-DISC-03 — GET sur /.well-known/host-meta.json (si supporté)
# ---------------------------------------------------------------------------

@pytest.mark.roadmap("R3")
@pytest.mark.rfc("RFC 8040 §3.1")
@pytest.mark.rfc("RFC 6415 App. A")
def test_disc_03_host_meta_json(client, restconf_root):
    """T-DISC-03 : réponse JSON valide avec le lien ``restconf``.

    RFC 6415 App. A : la représentation JRD est RECOMMANDÉE mais optionnelle.
    Si elle est servie :

    - ``200 OK`` ;
    - ``Content-Type: application/json`` (« The server MUST include the HTTP
      Content-Type response header field with a value of application/json ») ;
    - document JSON objet contenant un tableau ``links`` ;
    - exactement un lien ``rel="restconf"`` (RFC 8040 §3.1) dont le ``href``
      identifie la racine.

    Si le serveur ne supporte pas JRD, il doit renvoyer ``404`` et le test
    est ignoré (``skip``).
    """
    response = get_host_meta_json(client)  # skip si 404

    assert get_content_type(response) == JSON_CONTENT_TYPE, (
        f"Content-Type attendu {JSON_CONTENT_TYPE!r} (RFC 6415 App. A), "
        f"obtenu {response.headers.get('content-type')!r}"
    )

    data, links = parse_json_links(response.text)
    assert isinstance(data, dict), "la réponse JRD doit être un objet JSON"
    assert isinstance(links, list) and links, (
        f"la réponse JRD doit contenir un tableau 'links' non vide "
        f"(RFC 6415 App. A) : {response.text!r}"
    )

    matches = restconf_links(links)
    assert len(matches) == 1, (
        f"exactement un lien 'restconf' est requis (RFC 8040 §3.1), "
        f"obtenu {len(matches)} : {links!r}"
    )
    href = matches[0].get("href")
    assert href, f"le lien 'restconf' doit porter un attribut href : {matches[0]!r}"
    assert_href_is_plain_root(href)
    assert href_to_path(href) == restconf_root, (
        f"le lien 'restconf' JRD doit pointer vers {restconf_root!r}, "
        f"obtenu {href!r}"
    )


# ---------------------------------------------------------------------------
# T-DISC-04 — Accès avec un token invalide
# ---------------------------------------------------------------------------

@pytest.mark.roadmap("R3")
@pytest.mark.rfc("RFC 8040 §3.1")
@pytest.mark.rfc("RFC 6415 §2")
@pytest.mark.rfc("RFC 9110 §11.6.1")
def test_disc_04_invalid_token(client, invalid_token):
    """T-DISC-04 : accessible ou erreur cohérente, sans fuite de données.

    La découverte de la racine est un endpoint public (RFC 8040 §3.1,
    RFC 6415 §2) : un jeton invalide ne doit en aucun cas provoquer d'erreur
    serveur (``5xx``). Le comportement attendu est que l'endpoint reste
    accessible (``200``) ; une réponse ``401``/``403`` est tolérée si le
    serveur choisit de rejeter un jeton mal formé, auquel cas une réponse
    ``401`` doit s'accompagner de l'en-tête ``WWW-Authenticate``
    (RFC 9110 §11.6.1). Aucune donnée sensible ne doit fuiter.
    """
    response = client.get(
        HOST_META,
        headers={**ACCEPT_XRD, "Authorization": f"Bearer {invalid_token}"},
    )

    assert response.status_code < 500, (
        f"un jeton invalide ne doit pas provoquer d'erreur serveur, "
        f"obtenu {response.status_code}"
    )
    assert response.status_code in (200, 401, 403), (
        f"réponse incohérente pour un jeton invalide sur un endpoint public : "
        f"{response.status_code}"
    )

    if response.status_code == 200:
        # L'endpoint reste accessible : la découverte doit fonctionner.
        _, links = parse_xrd(response.text)
        assert restconf_href(links) is not None, (
            "le lien 'restconf' doit être présent lorsque l'endpoint est accessible"
        )
    elif response.status_code == 401:
        assert "www-authenticate" in response.headers, (
            "une réponse 401 doit inclure l'en-tête WWW-Authenticate "
            "(RFC 9110 §11.6.1)"
        )

    # Dans tous les cas, aucune donnée sensible ne doit fuiter.
    assert_no_sensitive_body(response.text)


# ---------------------------------------------------------------------------
# T-DISC-05 — Absence de données sensibles dans host-meta
# ---------------------------------------------------------------------------

@pytest.mark.roadmap("R3")
@pytest.mark.rfc("RFC 8040 §3.1")
@pytest.mark.rfc("RFC 6415 §3")
@pytest.mark.rfc("RFC 6415 §5")
@pytest.mark.parametrize("path", [HOST_META, HOST_META_JSON], ids=["xrd", "json"])
def test_disc_05_no_sensitive_data(client, path, restconf_root):
    """T-DISC-05 : la réponse ne contient aucune information de config ou d'état.

    RFC 6415 définit host-meta comme un document de *métadonnées d'hôte*
    (liens et propriétés) ; il ne doit exposer aucune donnée de configuration
    ou d'état du datastore, ni aucun secret. On vérifie :

    1. l'absence de tout mot-clé sensible dans le corps brut ;
    2. la conformité de la structure au vocabulaire autorisé :
       - XRD : seuls les éléments du schéma XRD 1.0 sont présents
         (RFC 6415 §3) ;
       - JRD : seules les clés ``subject``/``expires``/``aliases``/
         ``properties``/``links`` sont présentes (RFC 6415 App. A) ;
    3. l'absence de donnée sensible dans chacune des valeurs du document ;
    4. le ``href`` de la racine est un chemin simple, sans query ni fragment.
    """
    if path == HOST_META_JSON:
        response = get_host_meta_json(client)  # skip si 404
    else:
        response = client.get(path, headers=ACCEPT_XRD)
        assert response.status_code == 200

    # 1) Vérification textuelle globale.
    assert_no_sensitive_body(response.text)

    if path == HOST_META:
        root, links = parse_xrd(response.text)
        assert root.tag == f"{{{XRD_NS}}}XRD"

        # 2) Seuls les éléments du vocabulaire XRD 1.0 sont autorisés.
        for element in root:
            local_name = element.tag.split("}", 1)[-1]
            assert local_name in XRD_ELEMENTS, (
                f"élément non prévu par le schéma XRD 1.0 (RFC 6415 §3) : "
                f"{element.tag!r}"
            )

        # 3) Aucune donnée sensible dans les valeurs (attributs + textes).
        for value in iter_xrd_values(root):
            assert_no_sensitive_text(value)
    else:
        data, links = parse_json_links(response.text)
        assert isinstance(data, dict)

        # 2) Seules les clés JRD définies par la RFC 6415 App. A.
        assert set(data.keys()) <= JRD_KEYS, (
            f"clés de premier niveau non prévues par le format JRD "
            f"(RFC 6415 App. A) : {sorted(set(data.keys()) - JRD_KEYS)!r}"
        )

        # 3) Aucune donnée sensible dans les valeurs feuilles.
        for value in iter_json_leaves(data):
            if isinstance(value, str):
                assert_no_sensitive_text(value)

    # 4) Le href de la racine est un chemin simple.
    href = restconf_href(links)
    assert href is not None, "lien 'restconf' absent de la réponse"
    assert_href_is_plain_root(href)
    assert href_to_path(href) == restconf_root
