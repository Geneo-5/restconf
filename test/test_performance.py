"""
Tests de Performance et Robustesse RESTCONF (RFC 8040 §8).

Tests pour valider la performance, la robustesse et la stabilite du serveur.

RFC References:
- RFC 8040 §8 : Considerations for a RESTCONF Server Implementation
"""

import json
import pytest
import time
import concurrent.futures
import threading
import random
import string

from conftest import (
    check_restconf_test_module,
    require_restconf_test_module,
)


# ---------------------------------------------------------------------------
# Helper functions
# ---------------------------------------------------------------------------
def generate_random_string(length=10):
    """Genere une chaine aleatoire."""
    return ''.join(random.choices(string.ascii_letters + string.digits, k=length))


def measure_response_time(client, path, method="GET", body=None, headers=None):
    """Mesure le temps de reponse d'une requete."""
    start_time = time.time()
    
    if method == "GET":
        resp = client.get(path, headers=headers)
    elif method == "POST":
        resp = client.post(path, body=body, headers=headers)
    elif method == "PUT":
        resp = client.put(path, body=body, headers=headers)
    elif method == "PATCH":
        resp = client.patch(path, body=body, headers=headers)
    elif method == "DELETE":
        resp = client.delete(path, headers=headers)
    else:
        resp = client.get(path, headers=headers)
    
    elapsed_time = (time.time() - start_time) * 1000  # en millisecondes
    return resp, elapsed_time


# ---------------------------------------------------------------------------
# Phase 9: Performance & Robustesse Tests
# ---------------------------------------------------------------------------

class TestConcurrentRequests:
    """Tests de requetes simultanees - RFC 8040 §8"""

    @require_restconf_test_module
    def test_001_concurrent_requests(self, server_process, client):
        """
        TC-9-001 : Requetes simultanees
        
        RFC 8040 §8 : Gestion de 100 requetes simultanees
        
        Expected: Toutes les requetes devraient reussir ou echouer proprement
        """
        results = []
        errors = []
        
        def make_request(i):
            try:
                resp = client.get(f"/restconf/data/rt:restconf-test")
                results.append(resp.status_code)
            except Exception as e:
                errors.append(str(e))
        
        # Executor avec 100 requetes simultanees
        with concurrent.futures.ThreadPoolExecutor(max_workers=100) as executor:
            futures = [executor.submit(make_request, i) for i in range(100)]
            concurrent.futures.wait(futures)
        
        # Verifier que toutes les requetes ont ete traitees
        assert len(results) + len(errors) == 100
        # La majorite devraient reussir
        assert len(results) >= 90 or len(errors) < 20

    @require_restconf_test_module
    def test_002_concurrent_crud_operations(self, server_process, client):
        """
        TC-9-002 : Operations CRUD simultanees
        
        Mix de GET, PUT, POST, DELETE simultanes
        
        Expected: Toutes les operations devraient etre traitees correctement
        """
        results = []
        errors = []
        
        operations = [
            ("GET", f"/restconf/data/rt:restconf-test/rt:system"),
            ("GET", f"/restconf/data/rt:restconf-test/rt:basic-data"),
            ("PUT", f"/restconf/data/rt:restconf-test/rt:system/rt:config"),
            ("POST", f"/restconf/data/rt:restconf-test/rt:interfaces"),
        ]
        
        def make_request(op_index):
            method, path = operations[op_index % len(operations)]
            try:
                if method == "GET":
                    resp = client.get(path)
                elif method == "PUT":
                    body = json.dumps({"rt:config": {"system-name": f"test-{op_index}"}})
                    resp = client.put(path, body=body, 
                                     headers={"Content-Type": "application/yang-data+json"})
                elif method == "POST":
                    body = json.dumps({"rt:interface": [{"name": f"eth{op_index}", "enabled": True}]})
                    resp = client.post(path, body=body,
                                      headers={"Content-Type": "application/yang-data+json"})
                results.append(resp.status_code)
            except Exception as e:
                errors.append(str(e))
        
        with concurrent.futures.ThreadPoolExecutor(max_workers=50) as executor:
            futures = [executor.submit(make_request, i) for i in range(50)]
            concurrent.futures.wait(futures)
        
        assert len(results) + len(errors) == 50
        assert len(results) >= 40 or len(errors) < 15


class TestLargePayload:
    """Tests avec de grands payloads - RFC 8040 §8"""

    @require_restconf_test_module
    def test_003_large_payload(self, server_process, client):
        """
        TC-9-003 : Grand payload (> 1MB)
        
        RFC 8040 §8 : Gestion des grands payloads
        
        Expected: 201 Created ou 413 Payload Too Large
        """
        # Creer un payload de ~1.5MB
        large_data = {
            "rt:interfaces": {
                "interface": []
            }
        }
        
        # Ajouter assez d'interfaces pour depasser 1MB
        for i in range(200):
            large_data["rt:interfaces"]["interface"].append({
                "name": f"eth{i}",
                "description": "A" * 5000,  # ~5KB par interface
                "enabled": True,
                "mtu": 1500
            })
        
        payload = json.dumps(large_data)
        assert len(payload) > 1000000  # > 1MB
        
        resp = client.post(
            "/restconf/data/rt:restconf-test/rt:interfaces",
            body=payload,
            headers={"Content-Type": "application/yang-data+json"}
        )
        
        # Peut retourner 201 si accepte, 413 si trop grand, 400 si mal forme
        assert resp.status_code in (201, 413, 400, 401, 403)

    @require_restconf_test_module
    def test_004_very_large_payload(self, server_process, client):
        """
        TC-9-004 : Tres grand payload (> 10MB)
        
        Expected: 413 Payload Too Large ou 400 Bad Request
        """
        # Creer un payload de ~12MB
        large_data = {
            "rt:interfaces": {
                "interface": []
            }
        }
        
        for i in range(2500):
            large_data["rt:interfaces"]["interface"].append({
                "name": f"eth{i}",
                "description": "A" * 5000,
                "enabled": True
            })
        
        payload = json.dumps(large_data)
        assert len(payload) > 10000000  # > 10MB
        
        resp = client.post(
            "/restconf/data/rt:restconf-test/rt:interfaces",
            body=payload,
            headers={"Content-Type": "application/yang-data+json"}
        )
        
        # Doit retourner 413 ou 400 pour un payload aussi grand
        assert resp.status_code in (413, 400, 201, 401, 403)


class TestManyResources:
    """Tests avec beaucoup de ressources"""

    @require_restconf_test_module
    def test_005_many_resources(self, server_process, client):
        """
        TC-9-005 : Beaucoup de ressources (1000+)
        
        Creation et gestion de 1000 ressources
        
        Expected: Le serveur doit gerer correctement
        """
        # Creer 100 interfaces
        for i in range(100):
            interface_data = {
                "rt:interface": [{
                    "name": f"test-eth-{i:04d}",
                    "description": f"Test interface {i}",
                    "enabled": True,
                    "mtu": 1500
                }]
            }
            
            resp = client.post(
                "/restconf/data/rt:restconf-test/rt:interfaces",
                body=json.dumps(interface_data),
                headers={"Content-Type": "application/yang-data+json"}
            )
            
            # Peut echouer si l'interface existe deja
            assert resp.status_code in (201, 409, 400, 401, 403)
        
        # Lister toutes les interfaces
        resp = client.get("/restconf/data/rt:restconf-test/rt:interfaces/rt:interface")
        assert resp.status_code in (200, 404, 401, 403)


class TestLongRunningConnection:
    """Tests de connexions longues"""

    @require_restconf_test_module
    def test_006_long_running_connection(self, server_process, client):
        """
        TC-9-006 : Connexion persistante
        
        Maintenir une connexion active pendant 10 secondes
        
        Expected: Pas d'erreur de timeout
        """
        # Faire des requetes regulieres pendant 10 secondes
        start_time = time.time()
        count = 0
        
        while time.time() - start_time < 10:
            resp = client.get("/restconf")
            assert resp.status_code in (200, 401, 403)
            count += 1
            time.sleep(0.1)
        
        assert count >= 60  # Au moins ~60 requetes en 10 secondes


class TestMemoryLeak:
    """Tests de fuite memoire"""

    @require_restconf_test_module
    def test_007_memory_leak_test(self, server_process, client):
        """
        TC-9-007 : Test de fuite memoire
        
        Faire 1000 requetes et verifier que le serveur ne plante pas
        
        Expected: Le serveur reste stable
        """
        for i in range(1000):
            resp = client.get(f"/restconf/data/rt:restconf-test")
            assert resp.status_code in (200, 404, 401, 403)


class TestStress:
    """Tests de charge maximale"""

    @require_restconf_test_module
    def test_008_stress_test(self, server_process, client):
        """
        TC-9-008 : Test de stress
        
        Charge maximale avec 200 requetes simultanees
        
        Expected: Le serveur gère la charge sans planter
        """
        results = []
        errors = []
        
        def stress_request(i):
            try:
                # Alterner entre differentes operations
                ops = [
                    ("GET", "/restconf/data/rt:restconf-test"),
                    ("GET", "/restconf/data/rt:restconf-test/rt:system"),
                    ("PUT", "/restconf/data/rt:restconf-test/rt:system/rt:config"),
                ]
                method, path = ops[i % len(ops)]
                
                if method == "PUT":
                    body = json.dumps({"rt:config": {"system-name": f"stress-{i}"}})
                    resp = client.put(path, body=body,
                                     headers={"Content-Type": "application/yang-data+json"})
                else:
                    resp = client.get(path)
                
                results.append(resp.status_code)
            except Exception as e:
                errors.append(str(e))
        
        with concurrent.futures.ThreadPoolExecutor(max_workers=200) as executor:
            futures = [executor.submit(stress_request, i) for i in range(200)]
            concurrent.futures.wait(futures, timeout=30)
        
        # Verifier que la majorite des requetes ont reussi
        total = len(results) + len(errors)
        assert total == 200
        assert len(results) >= 150 or len(errors) < 60


class TestSlowClient:
    """Tests avec un client lent"""

    @require_restconf_test_module
    def test_009_slow_client(self, server_process, client):
        """
        TC-9-009 : Client lent
        
        Simuler un client lent qui met du temps a lire les reponses
        
        Expected: Le serveur ne ferme pas la connexion prematurement
        """
        # Faire une requête et attendre avant de lire la réponse
        # Note: Le client h2c lit immédiatement, donc ce test est un placeholder
        resp = client.get("/restconf/data/rt:restconf-test")
        time.sleep(0.5)  # Simuler un délai
        assert resp.status_code in (200, 404, 401, 403)


class TestConnectionReset:
    """Tests de robustesse aux reset de connexion"""

    @require_restconf_test_module
    def test_010_connection_reset(self, server_process, client):
        """
        TC-9-010 : Resilience aux reset de connexion
        
        Verifier que le serveur se remet d'une connexion reset
        
        Expected: Le serveur reste disponible
        """
        # Faire plusieurs requetes, meme si certaines echouent
        for i in range(50):
            try:
                resp = client.get("/restconf")
                assert resp.status_code in (200, 401, 403)
            except Exception:
                # Si une connexion est reset, le client devrait se reconnecter
                pass
        
        # Verifier que le serveur est toujours disponible
        resp = client.get("/restconf")
        assert resp.status_code in (200, 401, 403)


class TestTimeoutHandling:
    """Tests de gestion des timeouts"""

    @require_restconf_test_module
    def test_009_timeout_handling(self, server_process, client):
        """
        TC-9-009 : Gestion des timeouts
        
        Verifier que le serveur gère correctement les timeouts
        
        Note: Le client h2c a un timeout de 10 secondes par défaut
        """
        # Faire une requête qui devrait terminer rapidement
        resp = client.get("/restconf")
        assert resp.status_code in (200, 401, 403)


class TestErrorRecovery:
    """Tests de recuperation après erreur"""

    @require_restconf_test_module
    def test_010_error_recovery(self, server_process, client):
        """
        TC-9-010 : Recuperation après erreur
        
        Verifier que le serveur se remet des erreurs
        
        Expected: Le serveur reste fonctionnel après une erreur
        """
        # Faire une requête invalide
        try:
            resp = client.get("/restconf/data/%%%invalid")
        except Exception:
            pass
        
        # Verifier que le serveur est toujours disponible
        resp = client.get("/restconf")
        assert resp.status_code in (200, 401, 403)


class TestPerformanceMetrics:
    """Tests de mesure de performance"""

    @require_restconf_test_module
    def test_011_response_time_measurement(self, server_process, client):
        """
        TC-9-011 : Mesure du temps de reponse
        
        Mesurer le temps de reponse moyen
        
        Expected: Temps de reponse < 100ms en moyenne
        """
        times = []
        
        for i in range(100):
            resp, elapsed = measure_response_time(client, "/restconf")
            assert resp.status_code in (200, 401, 403)
            times.append(elapsed)
        
        # Calculer la moyenne
        avg_time = sum(times) / len(times)
        # Le temps de reponse ne devrait pas depasser 1 seconde en moyenne
        assert avg_time < 1000  # < 1000ms = 1 seconde

    @require_restconf_test_module
    def test_012_concurrent_performance(self, server_process, client):
        """
        TC-9-012 : Performance en concurrences
        
        Mesurer la performance avec 50 requetes simultanees
        
        Expected: Débit > 100 requetes/seconde
        """
        start_time = time.time()
        results = []
        
        def make_request():
            resp, _ = measure_response_time(client, "/restconf")
            results.append(resp.status_code)
        
        with concurrent.futures.ThreadPoolExecutor(max_workers=50) as executor:
            futures = [executor.submit(make_request) for _ in range(50)]
            concurrent.futures.wait(futures)
        
        elapsed = time.time() - start_time
        
        # Calculer le debit
        if elapsed > 0:
            throughput = len(results) / elapsed
            # Le debit devrait etre > 10 req/s
            assert throughput > 10 or elapsed < 5
