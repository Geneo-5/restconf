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

Configuration via options CLI pytest (forwardées par build.sh / entrypoint.sh) :

    pytest --restconf-base-url=https://10.0.0.1 \
           --restconf-root=/restconf \
           --restconf-test-jwt=eyJhbG... \
           --restconf-backend-h2c-url=http://127.0.0.1:8080 \
           test/

Fallback : variables d'environnement RESTCONF_BASE_URL, RESTCONF_ROOT,
RESTCONF_TEST_JWT, RESTCONF_BACKEND_H2C_URL.
"""

from __future__ import annotations

import os

import httpx
import pytest

# ---------------------------------------------------------------------------
# Options CLI pytest
# ---------------------------------------------------------------------------

def pytest_addoption(parser: pytest.Parser) -> None:
    group = parser.getgroup("restconf", "RESTCONF conformance test configuration")
    group.addoption(
        "--restconf-base-url",
        default=None,
        help="Base URL du reverse proxy TLS (défaut : https://127.0.0.1)",
    )
    group.addoption(
        "--restconf-root",
        default=None,
        help="Racine RESTCONF (défaut : /restconf)",
    )
    group.addoption(
        "--restconf-test-jwt",
        default=None,
        help="JWT Bearer pour les tests authentifiés "
             "(défaut : JWT alg=none, claim name=admin)",
    )
    group.addoption(
        "--restconf-backend-h2c-url",
        default=None,
        help="URL directe du backend h2c (défaut : http://127.0.0.1:8080)",
    )
    group.addoption(
        "--restconf-backend-unix-socket",
        default="/run/restconf.socket",
        help="Chemin du socket Unix du backend h2c (défaut : /run/restconf.sock)",
    )
    group.addoption(
        "--restconf-backend-socket-mode",
        default="0o660",
        help="Mode octal attendu du socket Unix du backend (défaut : 0o770)",
    )
    group.addoption(
        "--restconf-backend-socket-group",
        default="haproxy",
        help="Group propriétaire attendu du socket Unix (défaut : haproxy)",
    )
    group.addoption(
        "--restconf-backend-socket-user",
        default=None,
        help="User propriétaire attendu du socket Unix (défaut : aucune vérification)",
    )

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

# ---------------------------------------------------------------------------
# Helpers de résolution : CLI > env > défaut
# ---------------------------------------------------------------------------

_JWT_DEFAULT = "eyJhbGciOiJub25lIn0.eyJuYW1lIjoiYWRtaW4ifQ."

def _resolve(request, cli_name, env_name, default):
    value = request.config.getoption(cli_name)
    if value is not None:
        return value
    return os.environ.get(env_name, default)

# ---------------------------------------------------------------------------
# Fixtures de configuration
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def base_url(request: pytest.FixtureRequest) -> str:
    """URL de base du reverse proxy TLS (ex. https://127.0.0.1)."""
    return _resolve(request, "restconf_base_url", "RESTCONF_BASE_URL", "https://127.0.0.1")


@pytest.fixture(scope="session")
def restconf_root(request: pytest.FixtureRequest) -> str:
    """Racine RESTCONF (ex. /restconf)."""
    return _resolve(request, "restconf_root", "RESTCONF_ROOT", "/restconf")


@pytest.fixture(scope="session")
def test_jwt(request: pytest.FixtureRequest) -> str | None:
    return _resolve(request, "restconf_test_jwt", "RESTCONF_TEST_JWT", _JWT_DEFAULT) or None


@pytest.fixture(scope="session")
def backend_h2c_url(request: pytest.FixtureRequest) -> str | None:
    """URL TCP directe du backend h2c, ou None si backend en socket Unix."""
    return _resolve(
        request,
        "restconf_backend_h2c_url",
        "RESTCONF_BACKEND_H2C_URL",
        "",  # défaut vide → None → backend en socket Unix
    ) or None

@pytest.fixture(scope="session")
def api_url(base_url: str, restconf_root: str) -> str:
    """URL complète de la ressource racine API (ex. https://127.0.0.1/restconf)."""
    return f"{base_url}{restconf_root}"

@pytest.fixture(scope="session")
def backend_unix_socket(request: pytest.FixtureRequest) -> str | None:
    """Chemin du socket Unix du backend, ou None si non applicable."""
    return _resolve(
        request,
        "restconf_backend_unix_socket",
        "RESTCONF_BACKEND_UNIX_SOCKET",
        "/run/restconf.sock",
    ) or None

@pytest.fixture(scope="session")
def backend_socket_mode(request: pytest.FixtureRequest) -> int:
    """Mode octal attendu du socket Unix (défaut : 0o660)."""
    raw = _resolve(request, "restconf_backend_socket_mode", "RESTCONF_BACKEND_SOCKET_MODE", "0o660")
    return int(raw, 8)


@pytest.fixture(scope="session")
def backend_socket_group(request: pytest.FixtureRequest) -> str | None:
    """Group propriétaire attendu du socket Unix."""
    return _resolve(request, "restconf_backend_socket_group", "RESTCONF_BACKEND_SOCKET_GROUP", "haproxy") or None


@pytest.fixture(scope="session")
def backend_socket_user(request: pytest.FixtureRequest) -> str | None:
    """User propriétaire attendu du socket Unix, ou None."""
    return _resolve(request, "restconf_backend_socket_user", "RESTCONF_BACKEND_SOCKET_USER", "") or None

# ---------------------------------------------------------------------------
# Fixtures d'authentification
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def auth_headers(test_jwt: str | None) -> dict[str, str]:
    """Headers d'authentification Bearer, ou dict vide si pas de JWT."""
    if test_jwt:
        return {"Authorization": f"Bearer {test_jwt}"}
    return {}


@pytest.fixture()
def require_jwt(test_jwt: str | None) -> str:
    """Skip le test si aucun JWT n'est configuré."""
    if not test_jwt:
        pytest.skip(
            "Aucun JWT configuré — passer --restconf-test-jwt=<token> "
            "ou définir RESTCONF_TEST_JWT"
        )
    return test_jwt

@pytest.fixture(scope="session")
def invalid_token() -> str:
    """JWT structurellement invalide pour les tests de rejet."""
    return "invalid.token.here"

# ---------------------------------------------------------------------------
# Fixtures client HTTP
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def http2_client(base_url: str) -> httpx.Client:
    """Client HTTP/2 (ALPN h2) vers le reverse proxy TLS."""
    with httpx.Client(
        base_url=base_url,
        http2=True,
        verify=False,
        timeout=30.0,
    ) as client:
        yield client


@pytest.fixture(scope="session")
def http1_client(base_url: str) -> httpx.Client:
    """Client HTTP/1.1 vers le reverse proxy TLS."""
    with httpx.Client(
        base_url=base_url,
        http2=False,
        verify=False,
        timeout=30.0,
    ) as client:
        yield client


@pytest.fixture(scope="session")
def client(http2_client: httpx.Client) -> httpx.Client:
    """Alias pour compatibilité (client HTTP/2 par défaut)."""
    return http2_client

