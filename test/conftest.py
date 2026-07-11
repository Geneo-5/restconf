"""
Configuration pytest et fixtures pour les tests RESTCONF.

Le serveur parle h2c (HTTP/2 Cleartext) en Prior Knowledge
(pas de HTTP/1.1 Upgrade). La classe H2cClient ci-dessous
gère le framing HTTP/2 directement via la bibliothèque h2.
"""
import json
import os
import socket
import subprocess
import signal
import time
from functools import wraps

import h2.config
import h2.connection
import h2.events
import pytest


# ---------------------------------------------------------------------------
# Configuration par défaut
# ---------------------------------------------------------------------------
DEFAULT_SERVER_BIN = "/usr/local/bin/restconf-server"
DEFAULT_PLUGIN_BIN = "/usr/bin/sysrepo-plugind"
DEFAULT_BIND_ADDR = "127.0.0.1"
DEFAULT_PORT = 8080
DEFAULT_TIMEOUT = 5

# Variables d'environnement pour override
SERVER_BIN = os.environ.get("RESTCONF_SERVER_BIN", DEFAULT_SERVER_BIN)
PLUGIN_BIN = os.environ.get("SYSREPO_PLUGIND_BIN", DEFAULT_PLUGIN_BIN)
BIND_ADDR = os.environ.get("RESTCONF_BIND_ADDR", DEFAULT_BIND_ADDR)
PORT = int(os.environ.get("RESTCONF_PORT", DEFAULT_PORT))


# ---------------------------------------------------------------------------
# Client h2c (HTTP/2 Cleartext Prior Knowledge)
# ---------------------------------------------------------------------------
class H2cResponse:
    """Réponse HTTP/2 reçue du serveur RESTCONF."""

    def __init__(self, status, headers, body):
        self.status_code = status
        self.headers = headers
        self.content = body or b""
        self.text = self.content.decode("utf-8", errors="replace")

    def json(self):
        """Parse le body en JSON."""
        return json.loads(self.text)

    def __repr__(self):
        return (
            f"<H2cResponse [{self.status_code}] "
            f"{len(self.content)} bytes>"
        )


class H2cClient:
    """
    Client HTTP/2 Cleartext (h2c) avec Prior Knowledge.

    Utilise la bibliothèque h2 pour gérer le framing HTTP/2
    directement sur une socket TCP, sans négociation HTTP/1.1.
    """

    def __init__(self, host, port, timeout=DEFAULT_TIMEOUT):
        self.host = host
        self.port = port
        self.timeout = timeout

    def _connect(self):
        """Établit une connexion h2c Prior Knowledge."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(self.timeout)
        sock.connect((self.host, self.port))

        config = h2.config.H2Configuration(
            client_side=True, header_encoding="utf-8"
        )
        conn = h2.connection.H2Connection(config=config)
        conn.initiate_connection()
        sock.sendall(conn.data_to_send())

        # Lire les SETTINGS initiaux du serveur
        data = sock.recv(65535)
        if data:
            conn.receive_data(data)
            sock.sendall(conn.data_to_send())

        return sock, conn

    def _request(self, method, path, headers=None, body=None):
        """
        Envoie une requête HTTP/2 et retourne la réponse.

        Chaque requête utilise une nouvelle connexion TCP car le
        serveur actuel ne multiplexe pas les sessions de manière
        persistante (une session = une connexion).
        """
        sock, conn = self._connect()
        try:
            return self._do_request(
                sock, conn, method, path, headers, body
            )
        finally:
            try:
                sock.close()
            except Exception:
                pass

    def _do_request(
        self, sock, conn, method, path, headers, body
    ):
        """Exécute la requête sur une connexion établie."""
        request_headers = [
            (":method", method),
            (":path", path),
            (":scheme", "http"),
            (":authority", f"{self.host}:{self.port}"),
        ]

        # Ajouter les headers custom
        if headers:
            for k, v in headers.items():
                request_headers.append((k.lower(), v))

        # Accept par défaut si non spécifié
        has_accept = any(
            k == "accept" for k, _ in request_headers
        )
        if not has_accept:
            request_headers.append(
                ("accept", "application/yang-data+json")
            )

        stream_id = conn.get_next_available_stream_id()
        end_stream = body is None
        conn.send_headers(
            stream_id, request_headers, end_stream=end_stream
        )

        if body is not None:
            if isinstance(body, str):
                body = body.encode("utf-8")
            # Handle HTTP/2 flow control: send data in chunks
            # that fit within the flow control window
            offset = 0
            while offset < len(body):
                window = conn.local_flow_control_window(stream_id)
                # Also respect max frame size (usually 16384)
                max_frame = min(
                    window,
                    conn.max_outbound_frame_size,
                    len(body) - offset,
                )
                if max_frame <= 0:
                    # Need to wait for WINDOW_UPDATE from server
                    sock.sendall(conn.data_to_send())
                    try:
                        resp_data = sock.recv(65535)
                        if resp_data:
                            events = conn.receive_data(resp_data)
                            sock.sendall(conn.data_to_send())
                            # Process any non-flow-control events
                            for ev in events:
                                if isinstance(
                                    ev,
                                    h2.events.ResponseReceived,
                                ):
                                    pass  # handled later
                                elif isinstance(
                                    ev,
                                    h2.events.DataReceived,
                                ):
                                    pass
                                elif isinstance(
                                    ev, h2.events.StreamEnded
                                ):
                                    sock.sendall(
                                        conn.data_to_send()
                                    )
                                    return H2cResponse(
                                        response_status,
                                        response_headers,
                                        response_body,
                                    )
                    except socket.timeout:
                        break
                    continue
                is_last = offset + max_frame >= len(body)
                conn.send_data(
                    stream_id,
                    body[offset : offset + max_frame],
                    end_stream=is_last,
                )
                sock.sendall(conn.data_to_send())
                offset += max_frame

        sock.sendall(conn.data_to_send())

        # Lire la réponse
        response_status = None
        response_headers = {}
        response_body = b""

        while True:
            try:
                data = sock.recv(65535)
                if not data:
                    break

                events = conn.receive_data(data)
                sock.sendall(conn.data_to_send())

                for event in events:
                    if isinstance(event, h2.events.ResponseReceived):
                        for name, value in event.headers:
                            if isinstance(name, bytes):
                                name = name.decode("utf-8")
                            if isinstance(value, bytes):
                                value = value.decode("utf-8")
                            if name == ":status":
                                response_status = int(value)
                            else:
                                response_headers[name] = value
                    elif isinstance(event, h2.events.DataReceived):
                        response_body += event.data
                        conn.acknowledge_received_data(
                            event.flow_controlled_length,
                            event.stream_id,
                        )
                    elif isinstance(event, h2.events.StreamEnded):
                        sock.sendall(conn.data_to_send())
                        return H2cResponse(
                            response_status,
                            response_headers,
                            response_body,
                        )
                    elif isinstance(event, h2.events.StreamReset):
                        return H2cResponse(
                            response_status,
                            response_headers,
                            response_body,
                        )
                    elif isinstance(
                        event, h2.events.ConnectionTerminated
                    ):
                        return H2cResponse(
                            response_status,
                            response_headers,
                            response_body,
                        )

                sock.sendall(conn.data_to_send())
            except socket.timeout:
                break

        return H2cResponse(
            response_status, response_headers, response_body
        )

    def get(self, path, headers=None):
        """Requête GET."""
        return self._request("GET", path, headers)

    def post(self, path, body=None, headers=None):
        """Requête POST."""
        return self._request("POST", path, headers, body)

    def put(self, path, body=None, headers=None):
        """Requête PUT."""
        return self._request("PUT", path, headers, body)

    def patch(self, path, body=None, headers=None):
        """Requête PATCH."""
        return self._request("PATCH", path, headers, body)

    def delete(self, path, headers=None):
        """Requête DELETE."""
        return self._request("DELETE", path, headers)

    def options(self, path, headers=None):
        """Requête OPTIONS."""
        return self._request("OPTIONS", path, headers)

    def head(self, path, headers=None):
        """Requête HEAD."""
        return self._request("HEAD", path, headers)


# ---------------------------------------------------------------------------
# Fixtures pytest
# ---------------------------------------------------------------------------
@pytest.fixture(scope="session")
def sysrepo_plugin_process():
    """
    Démarre sysrepo-plugind pour gérer les plugins YANG.
    Doit être démarré AVANT le serveur RESTCONF.
    """
    plugin_bin = PLUGIN_BIN
    
    if not os.path.exists(plugin_bin):
        # sysrepo-plugind peut être à un autre endroit
        for path in ["/usr/bin/sysrepo-plugind", "/usr/local/bin/sysrepo-plugind"]:
            if os.path.exists(path):
                plugin_bin = path
                break
        else:
            # sysrepo-plugind est requis par toute la suite : son absence
            # doit faire ECHOUER les tests (erreur), pas les skipper.
            pytest.fail(
                f"sysrepo-plugind non trouvé: {plugin_bin} "
                f"(voir la variable d'environnement SYSREPO_PLUGIND_BIN)"
            )
    
    # Vérifier si sysrepo-plugind est déjà en cours d'exécution
    try:
        result = subprocess.run(
            ["pgrep", "-x", "sysrepo-plugind"],
            capture_output=True,
            text=True,
            timeout=2
        )
        if result.returncode == 0:
            # Déjà en cours d'exécution, ne pas le redémarrer
            yield None
            return
    except Exception:
        pass
    
    # Démarrer sysrepo-plugind en arrière-plan
    proc = subprocess.Popen(
        [plugin_bin, "--debug", "--fatal-plugin-fail"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )

    time.sleep(0.5)
    yield proc
    
    # Arrêt de sysrepo-plugind
    time.sleep(0.5)
    proc.send_signal(signal.SIGTERM)
    print(proc.stdout.read().decode(errors='backslashreplace'))
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()

# Cache des modules YANG installés
_INSTALLED_MODULES_CACHE = None


def _fetch_installed_modules(client):
    """
    Récupère l'ensemble des modules YANG chargés.

    Le premier résultat HTTP 200 est conservé en cache pour toute la
    durée de la session de tests. Les réponses en erreur ne sont pas
    mémorisées.
    """
    global _INSTALLED_MODULES_CACHE

    if _INSTALLED_MODULES_CACHE is not None:
        return 200, _INSTALLED_MODULES_CACHE

    resp = client.get(
        "/restconf/data/ietf-yang-library:modules-state",
        headers={"Accept": "application/yang-data+json"},
    )

    if resp.status_code != 200:
        return resp.status_code, None

    data = resp.json()
    modules_state = data.get(
        "ietf-yang-library:modules-state", data
    )
    modules = {
        m.get("name")
        for m in modules_state.get("module", [])
        if m.get("name")
    }

    _INSTALLED_MODULES_CACHE = modules
    return 200, modules


def _require_module(client, module_name, yang_file_hint):
    """
    Skippe le test courant si `module_name` n'apparait pas dans
    ietf-yang-library:modules-state.

    Si le serveur repond 401/403, la verification est impossible
    sans etre authentifie : on laisse le test s'executer, il
    rencontrera le meme mur d'authentification et pourra l'asserter
    lui-meme. Tout autre statut inattendu (ex. 5xx, ou un 204 qui
    serait lui-meme un bug de non-conformite) fait echouer le test
    en erreur plutot que de le skipper silencieusement.
    """
    status, modules = _fetch_installed_modules(client)

    if status in (401, 403):
        return
    if status != 200:
        pytest.fail(
            "GET ietf-yang-library:modules-state a echoue "
            f"(status={status}) - impossible de verifier la "
            f"presence du module '{module_name}'"
        )
    if module_name not in modules:
        pytest.skip(
            f"Module {yang_file_hint} non charge sur le serveur "
            "(absent de ietf-yang-library:modules-state)"
        )


def check_restconf_test_module(client):
    """
    Verifie que le module restconf-test.yang est charge.

    Tous les tests de qualification DOIVENT verifier cela avant
    execution.
    """
    _require_module(client, "restconf-test", "restconf-test.yang")


def check_oven_module(client):
    """Verifie que le module oven.yang est charge."""
    _require_module(client, "oven", "oven.yang")


def _find_client_arg(args, kwargs):
    """Retrouve l'instance H2cClient dans les args/kwargs d'un test."""
    if "client" in kwargs:
        return kwargs["client"]
    for arg in args:
        if hasattr(arg, "get") and hasattr(arg, "_connect"):
            return arg
    return None


def require_restconf_test_module(func):
    """Decorator pour verifier que restconf-test.yang est charge."""
    @wraps(func)
    def wrapper(*args, **kwargs):
        client_arg = _find_client_arg(args, kwargs)
        if client_arg:
            check_restconf_test_module(client_arg)
        return func(*args, **kwargs)
    return wrapper


def require_oven_module(func):
    """Decorator pour verifier que oven.yang est charge."""
    @wraps(func)
    def wrapper(*args, **kwargs):
        client_arg = _find_client_arg(args, kwargs)
        if client_arg:
            check_oven_module(client_arg)
        return func(*args, **kwargs)
    return wrapper


@pytest.fixture(scope="session")
def server_process(sysrepo_plugin_process):
    """
    Démarre le serveur RESTCONF pour la session de test.
    Le serveur est arrêté à la fin de la session.
    
    Requiert que sysrepo-plugind soit démarré au préalable.
    """
    server_bin = os.path.abspath(SERVER_BIN)

    if not os.path.exists(server_bin):
        pytest.skip(f"Serveur non trouvé: {server_bin}")

    # Démarrage du serveur avec JWT insecure pour les tests
    proc = subprocess.Popen(
        [
            #"gdb", "-ex", "run", "-ex", "bt", "--args", 
            server_bin,
            "-a", BIND_ADDR,
            "-p", str(PORT),
            "-v", "0",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )

    # Attendre que le serveur soit prêt (h2c Prior Knowledge)
    start_time = time.time()
    ready = False

    while time.time() - start_time < DEFAULT_TIMEOUT:
        try:
            client = H2cClient(BIND_ADDR, PORT, timeout=2)
            resp = client.get("/restconf")
            if resp.status_code in (200, 401, 403):
                ready = True
                break
        except (
            ConnectionRefusedError,
            OSError,
            socket.timeout,
            Exception,
        ):
            time.sleep(0.1)

    if not ready:
        proc.terminate()
        proc.wait()
        stdout = proc.stdout.read().decode() if proc.stdout else ""
        pytest.fail(
            f"Le serveur n'a pas démarré dans les "
            f"{DEFAULT_TIMEOUT}s\n"
            f"stdout: {stdout}"
        )

    yield proc

    # Arrêt du serveur
    time.sleep(1)
    proc.send_signal(signal.SIGTERM)
    print(proc.stdout.read().decode(errors='backslashreplace'))
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
def client(server_process):
    """
    Retourne un client h2c configuré pour RESTCONF.

    Ce client parle HTTP/2 Cleartext (h2c) en Prior Knowledge,
    conformément à l'architecture du serveur RESTCONF.
    
    Depend de server_process pour s'assurer que le serveur est demarre.
    """
    return H2cClient(BIND_ADDR, PORT)
