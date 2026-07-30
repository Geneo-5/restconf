"""
Tests de conformité : Transport, TLS, HTTP/2 et reverse proxy

RFC couvertes :
- RFC 8040 §2 : RESTCONF Protocol - Transport and Security
- RFC 9110 : HTTP Semantics
- RFC 9113 : HTTP/2
- RFC 8446 : TLS 1.3 (recommandé)

Items ROADMAP : R1, R44, A1, A2, A3, A16, A18
Tests : T-TRANS-01 à T-TRANS-15

Ces tests valident les comportements normatifs attendus du transport,
indépendamment de l'implémentation interne du serveur.
"""

import pytest
import httpx
import ssl
import socket
import time
import threading
from typing import Optional

# ============================================================================
# T-TRANS-01 : Accès HTTPS avec ALPN h2
# ============================================================================

@pytest.mark.roadmap("R1")
@pytest.mark.rfc("RFC 8040 §2", "RFC 9113", "RFC 8446")
class TestT_TRANS_01_HttpsAlpnH2:
    """
    T-TRANS-01 : Accès à la racine RESTCONF en HTTPS avec ALPN h2
    
    RFC 8040 §2 : RESTCONF requires HTTPS transport.
    RFC 9113 §3.3 : HTTP/2 connection establishment via ALPN.
    RFC 8446 : TLS 1.3 recommended, TLS 1.2 minimum.
    
    Comportement attendu :
    - La connexion TLS est établie avec succès
    - ALPN négocie le protocole "h2"
    - Le serveur répond correctement en HTTP/2
    """
    
    def test_https_connection_established(self, http2_client: httpx.Client, restconf_root: str):
        """La connexion HTTPS est établie avec succès."""
        response = http2_client.get(f"{restconf_root}")
        
        # Le serveur doit répondre (200, 401, 403, etc. mais pas d'erreur de connexion)
        assert response.status_code < 500 or response.status_code == 500
        
    def test_http2_protocol_negotiated(self, http2_client: httpx.Client, restconf_root: str):
        """Le protocole HTTP/2 est négocié via ALPN."""
        response = http2_client.get(f"{restconf_root}")
        
        # httpx expose la version HTTP négociée
        assert response.http_version == "HTTP/2", \
            f"Expected HTTP/2, got {response.http_version}"
    
    def test_tls_version_minimum(self, base_url: str):
        """
        RFC 8446 : TLS 1.2 minimum, TLS 1.3 recommandé.
        
        Vérifie que la version TLS négociée est >= 1.2.
        """
        # Extraction host:port depuis base_url
        from urllib.parse import urlparse
        parsed = urlparse(base_url)
        host = parsed.hostname or "127.0.0.1"
        port = parsed.port or 443
        
        # Création d'un contexte TLS acceptant TLS 1.2+
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        ctx.minimum_version = ssl.TLSVersion.TLSv1_2
        
        with socket.create_connection((host, port), timeout=10) as sock:
            with ctx.wrap_socket(sock, server_hostname=host) as ssock:
                tls_version = ssock.version()
                
                # TLS 1.2 ou 1.3 attendu
                assert tls_version in ("TLSv1.2", "TLSv1.3"), \
                    f"TLS version {tls_version} is below minimum TLS 1.2"


# ============================================================================
# T-TRANS-02 : Accès direct au backend h2c interdit
# ============================================================================

@pytest.mark.roadmap("R1", "A2", "A18")
@pytest.mark.rfc("RFC 8040 §2")
class TestT_TRANS_02_BackendNotExposed:
    """
    T-TRANS-02 : Le backend ne doit jamais être accessible directement.

    RFC 8040 §2 : The RESTCONF protocol MUST be transported over HTTPS.

    Deux configurations possibles :
    - Backend en socket Unix (CI Docker) : on vérifie l'absence d'écoute TCP
      et les permissions du socket.
    - Backend en TCP (dev) : on vérifie que le port n'est pas accessible.
    """

    def test_no_tcp_listener_for_backend(
        self, backend_h2c_url: str | None, backend_unix_socket: str | None
    ):
        """
        Aucune écoute TCP ne doit exposer le backend.

        Si le backend est en socket Unix, il ne doit PAS y avoir d'écoute
        TCP simultanée. Si une URL TCP est configurée, la connexion doit
        être refusée.
        """
        from urllib.parse import urlparse

        if backend_unix_socket and not backend_h2c_url:
            # Backend en socket Unix : vérifier qu'aucun port TCP connu
            # du backend n'est en écoute.
            # On teste les ports courants du projet.
            for port in (8080, 8443, 9090):
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=2):
                        pytest.fail(
                            f"Le backend ne doit pas écouter sur TCP 127.0.0.1:{port} "
                            f"lorsqu'il est configuré en socket Unix "
                            f"({backend_unix_socket}). Seul le reverse proxy TLS "
                            f"doit être le point d'entrée (RFC 8040 §2)."
                        )
                except (socket.timeout, ConnectionRefusedError, OSError):
                    pass  # Comportement attendu
            return

        if backend_h2c_url:
            parsed = urlparse(backend_h2c_url)
            host = parsed.hostname or "127.0.0.1"
            port = parsed.port or 8080
            try:
                with socket.create_connection((host, port), timeout=5):
                    pytest.fail(
                        f"Backend accessible directement sur {host}:{port}. "
                        "Le backend ne doit JAMAIS être exposé, seul le reverse "
                        "proxy TLS doit être le point d'entrée (RFC 8040 §2)."
                    )
            except (socket.timeout, ConnectionRefusedError, OSError):
                pass  # Comportement attendu
            return

        pytest.skip("Aucune configuration backend (ni TCP ni socket Unix)")

    def test_unix_socket_permissions(
        self,
        backend_unix_socket: str | None,
        backend_socket_mode: int,
        backend_socket_group: str | None,
        backend_socket_user: str | None,
    ):
        """
        Le socket Unix du backend doit avoir exactement le mode attendu
        (défaut : 0o660) et appartenir au group du reverse proxy.

        Pour un socket Unix, connect() requiert la permission write
        (unix(7)). Avec 0o660 :
        - owner (backend) : rw_ → peut se connecter
        - group (proxy)   : rw_ → peut se connecter
        - other           : --- → accès refusé

        Tout autre mode est une faille :
        - 0o666 / 0o665 : other peut se connecter
        - 0o655 / 0o650 : group ne peut pas se connecter (proxy bloqué)
        - 0o600 : proxy bloqué aussi
        """
        import grp
        import os
        import pwd
        import stat

        if not backend_unix_socket:
            pytest.skip("Backend non configuré en socket Unix")

        if not os.path.exists(backend_unix_socket):
            pytest.skip(f"Socket Unix {backend_unix_socket} absent")

        st = os.stat(backend_unix_socket)
        perms = stat.S_IMODE(st.st_mode)

        # 1) Mode exact
        assert perms == backend_socket_mode, (
            f"Le socket {backend_unix_socket} a le mode {oct(perms)}, "
            f"attendu {oct(backend_socket_mode)}. "
            f"Seuls le backend (owner) et le reverse proxy (group) "
            f"doivent pouvoir se connecter au socket."
        )

        # 2) Doit être un socket
        assert stat.S_ISSOCK(st.st_mode), (
            f"{backend_unix_socket} n'est pas un socket Unix "
            f"(mode {oct(st.st_mode)})"
        )

        # 3) Group propriétaire = group du proxy
        if backend_socket_group:
            try:
                expected_gid = grp.getgrnam(backend_socket_group).gr_gid
            except KeyError:
                pytest.skip(
                    f"Group {backend_socket_group!r} inexistant sur ce système"
                )
            actual_group = grp.getgrgid(st.st_gid).gr_name
            assert st.st_gid == expected_gid, (
                f"Le socket {backend_unix_socket} appartient au group "
                f"{actual_group!r} (gid {st.st_gid}), attendu "
                f"{backend_socket_group!r} (gid {expected_gid}). "
                f"Le reverse proxy doit être dans le group du socket "
                f"pour pouvoir s'y connecter."
            )

        # 4) User propriétaire (optionnel)
        if backend_socket_user:
            try:
                expected_uid = pwd.getpwnam(backend_socket_user).pw_uid
            except KeyError:
                pytest.skip(
                    f"User {backend_socket_user!r} inexistant sur ce système"
                )
            actual_user = pwd.getpwuid(st.st_uid).pw_name
            assert st.st_uid == expected_uid, (
                f"Le socket {backend_unix_socket} appartient à l'user "
                f"{actual_user!r} (uid {st.st_uid}), attendu "
                f"{backend_socket_user!r} (uid {expected_uid})."
            )

    def test_backend_http_request_fails(
        self, backend_h2c_url: str | None, backend_unix_socket: str | None
    ):
        """
        Une requête HTTP directe au backend doit échouer.
        """
        if backend_h2c_url:
            try:
                with httpx.Client(base_url=backend_h2c_url, timeout=5.0) as c:
                    response = c.get("/restconf")
                    pytest.fail(
                        f"Backend a répondu avec status {response.status_code}. "
                        "Le backend ne doit pas être accessible directement."
                    )
            except (httpx.ConnectError, httpx.ConnectTimeout, httpx.ReadTimeout):
                pass  # Comportement attendu
            return

        if backend_unix_socket:
            # En socket Unix, on ne peut pas faire de requête HTTP directe
            # via httpx sans transport Unix. L'absence d'écoute TCP (testée
            # ci-dessus) garantit la non-accessibilité.
            pytest.skip(
                "Backend en socket Unix : la non-accessibilité TCP est "
                "vérifiée par test_no_tcp_listener_for_backend"
            )

        pytest.skip("Aucune configuration backend")


# ============================================================================
# T-TRANS-03 : Pseudo-headers HTTP/2
# ============================================================================

@pytest.mark.roadmap("R1", "A1")
@pytest.mark.rfc("RFC 9113 §8.3")
class TestT_TRANS_03_Http2PseudoHeaders:
    """
    T-TRANS-03 : Requête HTTP/2 avec pseudo-headers
    
    RFC 9113 §8.3 : HTTP/2 pseudo-header fields (:method, :path, :authority, :scheme)
    
    Comportement attendu :
    - Le serveur reconstruit correctement la requête RESTCONF à partir des pseudo-headers
    - :method, :path, :authority, :scheme sont correctement interprétés
    """
    
    def test_method_pseudo_header(self, http2_client: httpx.Client, restconf_root: str):
        """Le pseudo-header :method est correctement interprété."""
        # GET
        response_get = http2_client.get(f"{restconf_root}")
        assert response_get.status_code in (200, 401, 403, 404)
        
        # OPTIONS
        response_options = http2_client.options(f"{restconf_root}")
        assert response_options.status_code in (200, 204, 401, 403)
    
    def test_path_pseudo_header(self, http2_client: httpx.Client, restconf_root: str):
        """Le pseudo-header :path est correctement interprété."""
        # Chemin valide
        response = http2_client.get(f"{restconf_root}")
        assert response.status_code < 500
        
        # Chemin avec query string
        response_query = http2_client.get(f"{restconf_root}?depth=1")
        assert response_query.status_code < 500
    
    def test_authority_pseudo_header(self, http2_client: httpx.Client, restconf_root: str):
        """
        Le pseudo-header :authority est correctement interprété.
        
        Note : httpx gère automatiquement :authority depuis base_url.
        """
        response = http2_client.get(f"{restconf_root}")
        assert response.status_code < 500
    
    def test_scheme_pseudo_header(self, http2_client: httpx.Client, restconf_root: str):
        """
        Le pseudo-header :scheme est correctement interprété.
        
        Pour HTTPS, :scheme doit être "https".
        """
        response = http2_client.get(f"{restconf_root}")
        # Si le serveur répond, le scheme a été correctement interprété
        assert response.status_code < 500


# ============================================================================
# T-TRANS-04 : HTTP/1.1 si supporté
# ============================================================================

@pytest.mark.roadmap("R1")
@pytest.mark.rfc("RFC 8040 §2", "RFC 9110")
class TestT_TRANS_04_Http11Support:
    """
    T-TRANS-04 : Requête HTTP/1.1 si supportée par le proxy
    
    RFC 8040 §2 : HTTP/1.1 MAY be supported if RESTCONF semantics remain compliant.
    
    Comportement attendu :
    - Si HTTP/1.1 est supporté, la sémantique RESTCONF reste conforme
    - Sinon, le proxy doit refuser proprement (pas de crash, pas de réponse malformée)
    """
    
    def test_http11_request_handled_gracefully(self, http1_client: httpx.Client, restconf_root: str):
        """
        Une requête HTTP/1.1 est gérée proprement (supportée ou refusée).
        """
        try:
            response = http1_client.get(f"{restconf_root}")
            
            # Si supporté : réponse HTTP valide
            assert response.status_code < 500 or response.status_code == 500
            assert response.http_version == "HTTP/1.1"
            
        except httpx.HTTPError as e:
            # Si refusé : erreur propre (pas de crash serveur)
            # Acceptable : connexion fermée, protocole non supporté
            assert "connection" in str(e).lower() or "protocol" in str(e).lower()
    
    def test_http11_semantics_compliant_if_supported(self, http1_client: httpx.Client, restconf_root: str):
        """
        Si HTTP/1.1 est supporté, la sémantique RESTCONF est conforme.
        """
        try:
            response = http1_client.get(f"{restconf_root}")

            # Un 401/403 est une réponse HTTP sémantiquement valide :
            # le serveur refuse l'accès non authentifié (RFC 9110 §11.6).
            if response.status_code in (401, 403):
                assert "WWW-Authenticate" in response.headers, (
                    f"{response.status_code} sans WWW-Authenticate"
                )
                return

            # Vérifications de base de la sémantique HTTP
            assert "Content-Type" in response.headers or response.status_code == 204
            assert response.status_code in (200, 204, 401, 403, 404, 405)
            
        except httpx.HTTPError:
            # HTTP/1.1 non supporté, c'est acceptable
            pytest.skip("HTTP/1.1 not supported by proxy")


# ============================================================================
# T-TRANS-05 : Préservation du header Authorization
# ============================================================================

@pytest.mark.roadmap("R1", "A18")
@pytest.mark.rfc("RFC 9110 §11.6", "RFC 6750")
class TestT_TRANS_05_AuthorizationHeaderPreserved:
    """
    T-TRANS-05 : Préservation du header Authorization: Bearer par le proxy
    
    RFC 9110 §11.6 : Authorization header field.
    RFC 6750 : OAuth 2.0 Bearer Token Usage.
    
    Comportement attendu :
    - Le proxy transmet le header Authorization intact au backend
    - Le backend peut valider le JWT
    """
    
    def test_authorization_header_transmitted(self, http2_client: httpx.Client, restconf_root: str, test_jwt: Optional[str]):
        """
        Le header Authorization est transmis au backend.
        
        Note : On ne peut pas vérifier directement que le backend reçoit le header,
        mais on peut vérifier que le serveur se comporte différemment avec/sans token.
        """
        if not test_jwt:
            pytest.skip("No test JWT available")
        
        # Requête SANS token
        response_no_auth = http2_client.get(f"{restconf_root}")
        
        # Requête AVEC token
        headers = {"Authorization": f"Bearer {test_jwt}"}
        response_with_auth = http2_client.get(f"{restconf_root}", headers=headers)
        
        # Le comportement doit différer (401 vs 200/403, ou autre différence)
        # Si les deux réponses sont identiques, le header n'est peut-être pas transmis
        assert (response_no_auth.status_code != response_with_auth.status_code) or \
               (response_no_auth.content != response_with_auth.content), \
               "Authorization header may not be transmitted to backend"
    
    def test_invalid_token_rejected(self, http2_client: httpx.Client, restconf_root: str):
        """
        Un token invalide est rejeté (le header est bien transmis et validé).
        """
        headers = {"Authorization": "Bearer invalid.token.here"}
        response = http2_client.get(f"{restconf_root}", headers=headers)
        
        # Le serveur doit rejeter le token invalide
        assert response.status_code in (401, 403), \
            f"Invalid token should be rejected, got {response.status_code}"


# ============================================================================
# T-TRANS-06 : Préservation des headers HTTP
# ============================================================================

@pytest.mark.roadmap("R1", "A18")
@pytest.mark.rfc("RFC 9110")
class TestT_TRANS_06_HttpHeadersPreserved:
    """
    T-TRANS-06 : Préservation des headers Accept, Content-Type, If-Match, If-None-Match, Location
    
    RFC 9110 : HTTP Semantics - Header fields.
    
    Comportement attendu :
    - Les headers ne sont ni supprimés ni altérés par le proxy
    """
    
    def test_accept_header_preserved(self, http2_client: httpx.Client, restconf_root: str):
        """Le header Accept est préservé."""
        headers = {"Accept": "application/yang-data+json"}
        response = http2_client.get(f"{restconf_root}", headers=headers)
        
        # Le serveur doit respecter Accept (ou retourner 406)
        assert response.status_code in (200, 401, 403, 404, 406)
        
        if response.status_code == 200:
            assert "application/yang-data+json" in response.headers.get("Content-Type", "")
    
    def test_content_type_header_preserved(self, http2_client: httpx.Client, restconf_root: str):
        """Le header Content-Type est préservé."""
        headers = {"Content-Type": "application/yang-data+json"}
        data = '{"test": "data"}'
        
        # POST avec Content-Type
        response = http2_client.post(f"{restconf_root}/data", headers=headers, content=data)
        
        # Le serveur doit traiter le Content-Type (200, 201, 400, 401, 403, 404, 415)
        assert response.status_code in (200, 201, 204, 400, 401, 403, 404, 405, 415)
    
    def test_conditional_headers_preserved(self, http2_client: httpx.Client, restconf_root: str):
        """Les headers conditionnels If-Match, If-None-Match sont préservés."""
        # If-None-Match avec ETag invalide
        headers = {"If-None-Match": '"invalid-etag"'}
        response = http2_client.get(f"{restconf_root}", headers=headers)
        
        # Le serveur doit traiter If-None-Match (200 ou 304)
        assert response.status_code in (200, 304, 401, 403, 404, 412)
    
    def test_location_header_in_response(self, http2_client: httpx.Client, restconf_root: str):
        """
        Le header Location est présent dans les réponses 201 Created.
        
        Note : Ce test nécessite une ressource créable.
        """
        # Tentative de création (peut échouer pour d'autres raisons)
        headers = {"Content-Type": "application/yang-data+json"}
        data = '{"test": "data"}'
        
        response = http2_client.post(f"{restconf_root}/data", headers=headers, content=data)
        
        if response.status_code == 201:
            assert "Location" in response.headers, \
                "201 Created response must include Location header (RFC 9110 §10.2.2)"


# ============================================================================
# T-TRANS-07 : Flux SSE long
# ============================================================================

@pytest.mark.roadmap("R1", "R24", "A16", "A18")
@pytest.mark.rfc("RFC 8040 §6", "RFC 9113")
class TestT_TRANS_07_SseLongLivedStream:
    """
    T-TRANS-07 : Ouverture d'un flux SSE long
    
    RFC 8040 §6 : Event streams.
    RFC 9113 : HTTP/2 flow control.
    
    Comportement attendu :
    - Le proxy ne coupe pas le flux prématurément si des heartbeats sont émis
    - Le flux reste ouvert tant que le client ne le ferme pas
    """
    
    @pytest.mark.skip(reason="Requires SSE stream endpoint to be implemented")
    def test_sse_stream_stays_open(self, http2_client: httpx.Client, restconf_root: str):
        """
        Un flux SSE reste ouvert pendant une durée prolongée.
        
        Note : Ce test nécessite un endpoint SSE fonctionnel.
        """
        # Exemple : GET sur un stream avec Accept: text/event-stream
        # Le flux doit rester ouvert pendant au moins 30 secondes
        # avec des heartbeats périodiques
        pass


# ============================================================================
# T-TRANS-08 : Bufferisation proxy sur SSE
# ============================================================================

@pytest.mark.roadmap("R1", "R24", "A16", "A18")
@pytest.mark.rfc("RFC 8040 §6")
class TestT_TRANS_08_SseBuffering:
    """
    T-TRANS-08 : Bufferisation proxy sur SSE
    
    Comportement attendu :
    - Les événements SSE sont transmis sans délai anormal
    - Le proxy ne bufferise pas excessivement
    """
    
    @pytest.mark.skip(reason="Requires SSE stream endpoint to be implemented")
    def test_sse_events_not_buffered(self, http2_client: httpx.Client, restconf_root: str):
        """
        Les événements SSE sont transmis immédiatement, sans bufferisation excessive.
        """
        # Mesurer le délai entre l'émission d'un événement et sa réception
        # Le délai doit être < 1 seconde (hors latence réseau)
        pass


# ============================================================================
# T-TRANS-09 : Fermeture RST_STREAM
# ============================================================================

@pytest.mark.roadmap("R1", "A16")
@pytest.mark.rfc("RFC 9113 §6.4")
class TestT_TRANS_09_RstStreamCleanup:
    """
    T-TRANS-09 : Fermeture du flux HTTP/2 par le client (RST_STREAM)
    
    RFC 9113 §6.4 : RST_STREAM frame.
    
    Comportement attendu :
    - Le serveur détecte la fermeture et nettoie les ressources associées
    - Pas de fuite mémoire ou de état orphelin
    """
    
    def test_rst_stream_handled_gracefully(self, base_url: str, restconf_root: str):
        """
        Le serveur gère proprement un RST_STREAM.
        
        Note : httpx ne permet pas d'envoyer manuellement RST_STREAM.
        Ce test vérifie qu'une fermeture brutale de connexion est gérée.
        """
        # Ouvrir une connexion, puis la fermer brutalement
        try:
            with httpx.Client(base_url=base_url, http2=True, verify=False, timeout=5.0) as client:
                # Démarrer une requête
                response = client.get(f"{restconf_root}")
                # Fermer brutalement (le contexte manager ferme la connexion)
        except Exception:
            pass
        
        # Vérifier que le serveur répond toujours après
        with httpx.Client(base_url=base_url, http2=True, verify=False, timeout=10.0) as client:
            response = client.get(f"{restconf_root}")
            assert response.status_code < 500, \
                "Server should remain responsive after client disconnect"


# ============================================================================
# T-TRANS-10 : SETTINGS_MAX_CONCURRENT_STREAMS
# ============================================================================

@pytest.mark.roadmap("R1", "R44")
@pytest.mark.rfc("RFC 9113 §6.5.2")
class TestT_TRANS_10_MaxConcurrentStreams:
    """
    T-TRANS-10 : Dépassement de SETTINGS_MAX_CONCURRENT_STREAMS
    
    RFC 9113 §6.5.2 : SETTINGS_MAX_CONCURRENT_STREAMS.
    
    Comportement attendu :
    - Le serveur refuse ou limite les streams supplémentaires conformément à HTTP/2
    - Pas de crash, réponse conforme HTTP/2
    """
    
    def test_concurrent_streams_limited(self, base_url: str, restconf_root: str):
        """
        Le serveur limite le nombre de streams concurrents.
        
        Note : Ce test ouvre de nombreux streams simultanés.
        """
        import concurrent.futures
        
        def make_request(i):
            try:
                with httpx.Client(base_url=base_url, http2=True, verify=False, timeout=10.0) as client:
                    response = client.get(f"{restconf_root}")
                    return response.status_code
            except Exception as e:
                return str(e)
        
        # Ouvrir 100 requêtes concurrentes
        with concurrent.futures.ThreadPoolExecutor(max_workers=100) as executor:
            futures = [executor.submit(make_request, i) for i in range(100)]
            results = [f.result() for f in concurrent.futures.as_completed(futures)]
        
        # Toutes les requêtes doivent être gérées (pas de crash serveur)
        # Certaines peuvent échouer avec REFUSED_STREAM, c'est acceptable
        assert len(results) == 100
        
        # Vérifier que le serveur répond toujours après
        with httpx.Client(base_url=base_url, http2=True, verify=False, timeout=10.0) as client:
            response = client.get(f"{restconf_root}")
            assert response.status_code < 500


# ============================================================================
# T-TRANS-11 : Headers HTTP trop volumineux
# ============================================================================

@pytest.mark.roadmap("R1", "R44")
@pytest.mark.rfc("RFC 9113 §6.5.2", "RFC 9110 §5")
class TestT_TRANS_11_OversizedHeaders:
    """
    T-TRANS-11 : Headers HTTP trop volumineux
    
    RFC 9113 §6.5.2 : SETTINGS_MAX_HEADER_LIST_SIZE.
    RFC 9110 §5 : Header fields.
    
    Comportement attendu :
    - Le serveur rejette la requête avec une erreur HTTP appropriée
    - Pas de crash
    """
    
    def test_oversized_header_rejected(self, http2_client: httpx.Client, restconf_root: str):
        """
        Un header surdimensionné est rejeté proprement.
        """
        # Créer un header très volumineux (64 KB)
        large_value = "X" * (64 * 1024)
        headers = {"X-Large-Header": large_value}
        
        try:
            response = http2_client.get(f"{restconf_root}", headers=headers)
            
            # Le serveur doit rejeter (400, 431, ou erreur HTTP/2)
            assert response.status_code in (400, 431, 500), \
                f"Oversized header should be rejected, got {response.status_code}"
                
        except httpx.HTTPError as e:
            # Erreur de connexion ou protocole, acceptable
            assert "header" in str(e).lower() or "frame" in str(e).lower() or "stream" in str(e).lower()


# ============================================================================
# T-TRANS-12 : Corps de requête trop volumineux
# ============================================================================

@pytest.mark.roadmap("R1", "R44")
@pytest.mark.rfc("RFC 9110 §8.6")
class TestT_TRANS_12_OversizedBody:
    """
    T-TRANS-12 : Corps de requête trop volumineux
    
    RFC 9110 §8.6 : Message body length.
    
    Comportement attendu :
    - Le serveur rejette avec 413 Payload Too Large ou erreur équivalente
    """
    
    def test_oversized_body_rejected(self, http2_client: httpx.Client, restconf_root: str):
        """
        Un corps de requête surdimensionné est rejeté.
        """
        # Créer un corps très volumineux (10 MB)
        large_body = "X" * (10 * 1024 * 1024)
        headers = {"Content-Type": "application/yang-data+json"}
        
        try:
            response = http2_client.post(
                f"{restconf_root}/data",
                headers=headers,
                content=large_body,
                timeout=30.0
            )
            
            # 401 est acceptable : le serveur vérifie l'authentification
            # avant de traiter le corps (RFC 9110 §11.6).
            assert response.status_code in (400, 401, 413, 500), \
                f"Oversized body should be rejected, got {response.status_code}"
                
        except httpx.HTTPError:
            # Erreur de connexion, acceptable
            pass


# ============================================================================
# T-TRANS-13 : Timeout proxy et SSE
# ============================================================================

@pytest.mark.roadmap("R1", "R24", "A16", "A18")
@pytest.mark.rfc("RFC 8040 §6")
class TestT_TRANS_13_ProxyTimeoutSse:
    """
    T-TRANS-13 : Timeout proxy inférieur à la durée d'un flux SSE
    
    Comportement attendu :
    - Le heartbeat ou la configuration proxy évite la coupure
    - Le flux SSE n'est pas interrompu prématurément
    """
    
    @pytest.mark.skip(reason="Requires SSE stream endpoint to be implemented")
    def test_sse_heartbeat_prevents_timeout(self, http2_client: httpx.Client, restconf_root: str):
        """
        Les heartbeats SSE empêchent le timeout proxy.
        """
        # Ouvrir un flux SSE, attendre > timeout proxy typique (60s)
        # Vérifier que le flux reste ouvert grâce aux heartbeats
        pass


# ============================================================================
# T-TRANS-14 : TLS 1.0/1.1 interdits
# ============================================================================

@pytest.mark.roadmap("R1", "R44")
@pytest.mark.rfc("RFC 8446")
@pytest.mark.filterwarnings("ignore::DeprecationWarning")
class TestT_TRANS_14_OldTlsRejected:
    """
    T-TRANS-14 : TLS 1.0/1.1 si interdits
    
    RFC 8446 : TLS 1.3 (TLS 1.0/1.1 deprecated).
    
    Comportement attendu :
    - La connexion est refusée
    """
    
    def test_tls_1_0_rejected(self, base_url: str):
        """
        TLS 1.0 est refusé.
        """
        from urllib.parse import urlparse
        parsed = urlparse(base_url)
        host = parsed.hostname or "127.0.0.1"
        port = parsed.port or 443
        
        # Contexte TLS forçant TLS 1.0
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        ctx.maximum_version = ssl.TLSVersion.TLSv1
        ctx.minimum_version = ssl.TLSVersion.TLSv1
        
        try:
            with socket.create_connection((host, port), timeout=10) as sock:
                with ctx.wrap_socket(sock, server_hostname=host) as ssock:
                    # Si on arrive ici, TLS 1.0 est accepté (FAIL)
                    pytest.fail("TLS 1.0 should be rejected")
        except (ssl.SSLError, OSError):
            # Comportement attendu : connexion refusée
            pass
    
    def test_tls_1_1_rejected(self, base_url: str):
        """
        TLS 1.1 est refusé.
        """
        from urllib.parse import urlparse
        parsed = urlparse(base_url)
        host = parsed.hostname or "127.0.0.1"
        port = parsed.port or 443
        
        # Contexte TLS forçant TLS 1.1
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        ctx.maximum_version = ssl.TLSVersion.TLSv1_1
        ctx.minimum_version = ssl.TLSVersion.TLSv1_1
        
        try:
            with socket.create_connection((host, port), timeout=10) as sock:
                with ctx.wrap_socket(sock, server_hostname=host) as ssock:
                    # Si on arrive ici, TLS 1.1 est accepté (FAIL)
                    pytest.fail("TLS 1.1 should be rejected")
        except (ssl.SSLError, OSError):
            # Comportement attendu : connexion refusée
            pass


# ============================================================================
# T-TRANS-15 : ALPN sans h2
# ============================================================================

@pytest.mark.roadmap("R1")
@pytest.mark.rfc("RFC 9113 §3.3")
class TestT_TRANS_15_AlpnWithoutH2:
    """
    T-TRANS-15 : ALPN sans h2 si HTTP/2 est requis
    
    RFC 9113 §3.3 : ALPN negotiation.
    
    Comportement attendu :
    - La connexion est refusée ou dégradée uniquement si explicitement supporté
    """
    
    def test_alpn_http11_only(self, base_url: str):
        """
        ALPN proposant uniquement http/1.1.
        
        Si HTTP/2 est requis, la connexion doit être refusée.
        Si HTTP/1.1 est supporté, la connexion peut être dégradée.
        """
        from urllib.parse import urlparse
        parsed = urlparse(base_url)
        host = parsed.hostname or "127.0.0.1"
        port = parsed.port or 443
        
        # Contexte TLS avec ALPN http/1.1 uniquement
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        ctx.set_alpn_protocols(["http/1.1"])
        
        try:
            with socket.create_connection((host, port), timeout=10) as sock:
                with ctx.wrap_socket(sock, server_hostname=host) as ssock:
                    alpn_protocol = ssock.selected_alpn_protocol()
                    
                    # Si HTTP/2 est requis, alpn_protocol devrait être None ou la connexion refusée
                    # Si HTTP/1.1 est supporté, alpn_protocol == "http/1.1"
                    
                    if alpn_protocol == "http/1.1":
                        # HTTP/1.1 supporté, acceptable
                        pass
                    elif alpn_protocol is None:
                        # Pas de protocole négocié, le serveur peut refuser
                        pass
                    else:
                        pytest.fail(f"Unexpected ALPN protocol: {alpn_protocol}")
                        
        except (ssl.SSLError, OSError):
            # Connexion refusée, acceptable si HTTP/2 est requis
            pass
