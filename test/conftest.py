# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN
"""Fixtures pytest partagées pour la suite de conformité RESTCONF.

Ces fixtures sont volontairement génériques afin d'être réutilisées par
l'ensemble des sections de tests décrites dans ``ROADMAP.md`` (découverte,
datastore, GET, écritures, NACM, etc.).

Environnement d'exécution attendu (CI Docker, voir ``docker/entrypoint.sh``) :

    client pytest ── HTTPS/HTTP2 (TLS) ──> reverse proxy (HAProxy)
                                              │  h2c "prior knowledge"
                                              ▼
                                          restconfd (backend)

Le serveur est exposé derrière un reverse proxy qui termine TLS sur
``https://127.0.0.1`` avec un certificat auto-signé (``docker/dev.pem``) ;
la vérification TLS est donc désactivée côté client (``verify=False``).

La configuration est pilotée par variables d'environnement afin de pouvoir
être injectée via ``docker run -e ...`` sans modifier le code :

``RESTCONF_BASE_URL``
    URL de base du reverse proxy (défaut : ``https://127.0.0.1``).
``RESTCONF_ROOT``
    Chemin de la racine RESTCONF ``{+restconf}`` exposée par le serveur
    (RFC 8040 §3.1), tel qu'annoncé par le lien ``restconf`` de host-meta
    (défaut : ``/restconf``, valeur utilisée dans les exemples de la RFC 8040).
``RESTCONF_TEST_JWT``
    JWT de test utilisé pour les requêtes authentifiées. Par défaut, le
    token ``alg=none`` (``name=admin``) déjà utilisé par ``scripts/curl.sh``.
"""

import os

import pytest

try:
    import httpx
except ImportError:  # pragma: no cover - dépendance manquante
    httpx = None


# JWT de test "alg=none" (claim name=admin), identique a scripts/curl.sh.
# Il n'est utilise que par les tests qui ont besoin d'une identite ; la
# decouverte de la racine RESTCONF (R3) est par definition non authentifiee.
DEFAULT_TEST_JWT = "eyJhbGciOiJub25lIn0.eyJuYW1lIjoiYWRtaW4ifQ."


def pytest_configure(config):
    """Enregistre les markers personnalisés de la suite de conformité."""
    config.addinivalue_line(
        "markers",
        "rfc(name): référence la RFC / section validée par le test",
    )
    config.addinivalue_line(
        "markers",
        "roadmap(item): référence l'item de la ROADMAP (ex: R3) couvert par le test",
    )


@pytest.fixture(scope="session")
def base_url() -> str:
    """URL de base du reverse proxy exposant le backend RESTCONF."""
    return os.environ.get("RESTCONF_BASE_URL", "https://127.0.0.1").rstrip("/")


@pytest.fixture(scope="session")
def restconf_root() -> str:
    """Racine RESTCONF ``{+restconf}`` attendue (RFC 8040 §3.1)."""
    root = os.environ.get("RESTCONF_ROOT", "/restconf")
    if not root.startswith("/"):
        root = "/" + root
    return root.rstrip("/") or "/"


@pytest.fixture(scope="session")
def test_jwt() -> str:
    """JWT de test valide (mode ``ALLOW_INSECURE_JWT=ON`` du CI)."""
    return os.environ.get("RESTCONF_TEST_JWT", DEFAULT_TEST_JWT)


@pytest.fixture(scope="session")
def auth_headers(test_jwt) -> dict:
    """En-têtes d'authentification ``Authorization: Bearer`` pour un token valide."""
    return {"Authorization": f"Bearer {test_jwt}"}


@pytest.fixture(scope="session")
def invalid_token() -> str:
    """Un token volontairement invalide / mal formé."""
    return "invalid.jwt.token"


@pytest.fixture()
def client(base_url):
    """Client HTTP/2 vers le reverse proxy, sans vérification TLS.

    ``http2=True`` permet de négocier HTTP/2 via ALPN (comme ``curl --http2``
    dans ``scripts/curl.sh``) ; si le proxy ne négocie pas ``h2``, httpx
    retombe automatiquement en HTTP/1.1, que le proxy traduit vers le backend
    h2c — les tests restent donc valables dans les deux cas.
    """
    if httpx is None:  # pragma: no cover
        pytest.skip("httpx n'est pas installé (voir test/requirements.txt)")

    with httpx.Client(
        base_url=base_url,
        http2=True,
        verify=False,  # certificat auto-signé du CI (docker/dev.pem)
        timeout=30.0,
    ) as http_client:
        yield http_client

@pytest.fixture()
def require_jwt(test_jwt: str | None):
    """Skip le test si aucun JWT n'est configuré."""
    if not test_jwt:
        pytest.skip(
            "RESTCONF_TEST_JWT non défini — "
            "test authentifié impossible sans token"
        )
    return test_jwt