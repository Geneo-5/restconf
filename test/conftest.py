"""
Configuration pytest et fixtures pour les tests RESTCONF.
"""
import os
import subprocess
import time
import signal
import pytest
import requests

# Configuration par défaut
DEFAULT_SERVER_BIN = "./build/restconf-server"
DEFAULT_BIND_ADDR = "127.0.0.1"
DEFAULT_PORT = 8080
DEFAULT_TIMEOUT = 5

# Variables d'environnement pour override
SERVER_BIN = os.environ.get("RESTCONF_SERVER_BIN", DEFAULT_SERVER_BIN)
BIND_ADDR = os.environ.get("RESTCONF_BIND_ADDR", DEFAULT_BIND_ADDR)
PORT = int(os.environ.get("RESTCONF_PORT", DEFAULT_PORT))


@pytest.fixture(scope="session")
def server_process():
    """
    Démarre le serveur RESTCONF pour la session de test.
    Le serveur est arrêté à la fin de la session.
    """
    server_bin = os.path.abspath(SERVER_BIN)
    
    if not os.path.exists(server_bin):
        pytest.skip(f"Serveur non trouvé: {server_bin}")
    
    # Démarrage du serveur
    proc = subprocess.Popen(
        [server_bin, "-a", BIND_ADDR, "-p", str(PORT)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    
    # Attendre que le serveur soit prêt
    base_url = f"http://{BIND_ADDR}:{PORT}"
    start_time = time.time()
    ready = False
    
    while time.time() - start_time < DEFAULT_TIMEOUT:
        try:
            resp = requests.get(f"{base_url}/restconf", timeout=1)
            if resp.status_code in (200, 401, 403):
                ready = True
                break
        except requests.exceptions.ConnectionError:
            time.sleep(0.1)
    
    if not ready:
        proc.terminate()
        proc.wait()
        pytest.fail(f"Le serveur n'a pas démarré dans les {DEFAULT_TIMEOUT}s")
    
    yield proc
    
    # Arrêt du serveur
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


@pytest.fixture
def base_url():
    """Retourne l'URL de base du serveur RESTCONF."""
    return f"http://{BIND_ADDR}:{PORT}"


@pytest.fixture
def session():
    """
    Session HTTP configurée pour RESTCONF.
    - Force HTTP/2 prior knowledge (via header Connection: Upgrade, HTTP2-Settings)
    - Accept par défaut: application/yang-data+json
    """
    s = requests.Session()
    s.headers.update({
        "Accept": "application/yang-data+json",
    })
    return s
