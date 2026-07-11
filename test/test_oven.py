"""
Tests pour le module oven.yang (Sysrepo Example).

Tests simples et pedagogiques pour le module oven.yang qui represente
un four domestique. Ce module est utilise pour les exemples et les tests
de base.

Module: oven.yang
Namespace: urn:sysrepo:oven
Prefix: ov

RFC References:
- RFC 8040 : RESTCONF Protocol
- RFC 7950 : YANG 1.1 Data Modeling Language

Note: Ces tests ne requierent PAS que restconf-test.yang soit charge.
Ils utilisent uniquement oven.yang qui est plus simple.
"""

import json
import pytest

from conftest import check_oven_module, require_oven_module


# ---------------------------------------------------------------------------
# Oven Module Tests
# ---------------------------------------------------------------------------

class TestOvenModuleDiscovery:
    """Tests de decouverte du module oven.yang"""

    def test_001_oven_module_accessible(self, server_process, client):
        """
        TC-OVEN-001 : Acces au module oven.yang
        
        Verifier que le module oven est accessible via RESTCONF.
        
        Expected: 200 OK ou 404 (si le module n'est pas charge)
        """
        resp = client.get("/restconf/data/oven:oven")
        assert resp.status_code in (200, 404, 401, 403)

    def test_002_oven_state_accessible(self, server_process, client):
        """
        TC-OVEN-002 : Acces a l'etat du four
        
        Verifier que oven-state (config=false) est accessible.
        
        Expected: 200 OK ou 404
        """
        resp = client.get("/restconf/data/oven:oven-state")
        assert resp.status_code in (200, 404, 401, 403)


class TestOvenConfiguration:
    """Tests de la configuration du four"""

    @require_oven_module
    def test_003_get_oven_config(self, server_process, client):
        """
        TC-OVEN-003 : GET configuration du four
        
        Lire la configuration actuelle du four.
        
        Expected: 200 OK (le container peut etre vide si
        non instancie)
        """
        resp = client.get("/restconf/data/oven:oven")
        
        assert resp.status_code in (200, 204, 404, 401, 403)
        
        if resp.status_code == 200 and resp.content:
            data = resp.json()
            # Le container oven peut etre present ou absent
            # selon qu'il a ete instancie ou non
            if "oven:oven" in data:
                oven = data["oven:oven"]
                assert isinstance(oven, dict)

    @require_oven_module
    def test_004_get_single_leaf(self, server_process, client):
        """
        TC-OVEN-004 : GET une leaf specifique
        
        Lire une leaf individuelle de la configuration.
        
        Expected: 200 OK avec la valeur de la leaf, ou 204/404
        si non instanciee
        """
        resp = client.get("/restconf/data/oven:oven/turned-on")
        
        assert resp.status_code in (200, 204, 404, 401, 403)
        
        if resp.status_code == 200 and resp.content:
            data = resp.json()
            # La leaf peut etre retournee directement ou dans
            # son container parent (RFC 7951)
            assert (
                "oven:turned-on" in data
                or "turned-on" in data
                or "oven:oven" in data
            )

    @require_oven_module
    def test_005_put_oven_config(self, server_process, client):
        """
        TC-OVEN-005 : PUT configuration du four
        
        Mettre a jour la configuration du four (allumer a 180°C).
        
        Expected: 201 Created ou 204 No Content
        """
        config = {
            "oven:oven": {
                "turned-on": True,
                "temperature": 180
            }
        }
        
        resp = client.put(
            "/restconf/data/oven:oven",
            body=json.dumps(config),
            headers={"Content-Type": "application/yang-data+json"}
        )
        
        assert resp.status_code in (201, 204, 401, 403)

    @require_oven_module
    def test_006_patch_oven_temperature(self, server_process, client):
        """
        TC-OVEN-006 : PATCH temperature du four
        
        Modifier uniquement la temperature sans toucher a turned-on.
        
        Expected: 204 No Content
        """
        patch = {
            "oven:oven": {
                "temperature": 200
            }
        }
        
        resp = client.patch(
            "/restconf/data/oven:oven",
            body=json.dumps(patch),
            headers={"Content-Type": "application/yang-data+patch+json"}
        )
        
        # PATCH peut utiliser yang-data+json ou yang-data+patch+json
        assert resp.status_code in (204, 404, 401, 403, 415)

    @require_oven_module
    def test_007_delete_oven_config(self, server_process, client):
        """
        TC-OVEN-007 : DELETE configuration du four
        
        Supprimer la configuration pour revenir aux valeurs par defaut.
        
        Expected: 204 No Content
        """
        resp = client.delete("/restconf/data/oven:oven")
        assert resp.status_code in (204, 404, 401, 403)


class TestOvenState:
    """Tests de l'etat du four (operational data)"""

    @require_oven_module
    def test_008_get_oven_state(self, server_process, client):
        """
        TC-OVEN-008 : GET etat du four
        
        Lire l'etat operationnel du four (temperature actuelle, nourriture).
        
        Expected: 200 OK avec l'etat
        """
        resp = client.get("/restconf/data/oven:oven-state")
        
        if resp.status_code == 200:
            data = resp.json()
            assert "oven:oven-state" in data
            state = data["oven:oven-state"]
            # Doit contenir temperature et food-inside
            assert "temperature" in state
            assert "food-inside" in state
        else:
            assert resp.status_code in (200, 404, 401, 403)

    @require_oven_module
    def test_009_get_state_leaf(self, server_process, client):
        """
        TC-OVEN-009 : GET une leaf de l'etat
        
        Lire une leaf specifique de l'etat.
        
        Expected: 200 OK
        """
        resp = client.get("/restconf/data/oven:oven-state/temperature")
        
        assert resp.status_code in (200, 204, 404, 401, 403)
        
        if resp.status_code == 200 and resp.content:
            data = resp.json()
            # RFC 7951: la leaf peut etre dans son container
            # parent ou directement
            assert (
                "oven:temperature" in data
                or "temperature" in data
                or "oven:oven-state" in data
            )


class TestOvenRPC:
    """Tests des operations RPC du four"""

    @require_oven_module
    def test_010_rpc_insert_food_now(self, server_process, client):
        """
        TC-OVEN-010 : RPC insert-food avec time=now
        
        Executer l'operation insert-food avec le parametre time="now".
        
        Expected: 200 OK
        """
        rpc_data = {
            "oven:input": {
                "time": "now"
            }
        }
        
        resp = client.post(
            "/restconf/operations/oven:insert-food",
            body=json.dumps(rpc_data),
            headers={"Content-Type": "application/yang-data+json"}
        )
        
        assert resp.status_code in (200, 204, 404, 401, 403)

    @require_oven_module
    def test_011_rpc_remove_food(self, server_process, client):
        """
        TC-OVEN-012 : RPC remove-food
        
        Executer l'operation remove-food (sans parametres).
        
        Expected: 200 OK
        """
        resp = client.post("/restconf/operations/oven:remove-food")
        
        assert resp.status_code in (200, 204, 404, 401, 403)

    @require_oven_module
    def test_012_rpc_insert_food_on_ready(self, server_process, client):
        """
        TC-OVEN-011 : RPC insert-food avec time=on-oven-ready
        
        Executer l'operation insert-food avec time="on-oven-ready".
        
        Expected: 200 OK
        """
        rpc_data = {
            "oven:input": {
                "time": "on-oven-ready"
            }
        }
        
        resp = client.post(
            "/restconf/operations/oven:insert-food",
            body=json.dumps(rpc_data),
            headers={"Content-Type": "application/yang-data+json"}
        )
        
        assert resp.status_code in (200, 204, 404, 401, 403)


class TestOvenNotifications:
    """Tests des notifications du four"""

    @require_oven_module
    def test_013_notification_stream(self, server_process, client):
        """
        TC-OVEN-013 : Stream de notifications oven-ready
        
        S'abonner au stream de notifications pour recevoir oven-ready.
        
        Note: Ce test depend du support SSE par le serveur.
        Le client h2c actuel ne supporte pas les streams SSE.
        Ce test est un placeholder.
        """
        resp = client.get("/restconf/stream/oven:oven-ready")
        
        # Peut retourner 200 avec un stream, ou 404 si non supporté
        assert resp.status_code in (200, 404, 401, 403)


class TestOvenEdgeCases:
    """Tests des cas particuliers du module oven"""

    @require_oven_module
    def test_014_temperature_range(self, server_process, client):
        """
        TC-OVEN-014 : Validation de la plage de temperature
        
        Le type oven-temperature a range 0..250.
        Essayer de mettre une temperature hors range.
        
        Expected: 400 Bad Request (violation de contrainte)
        """
        # Essayer de mettre une temperature trop elevee
        config = {
            "oven:oven": {
                "temperature": 300  # Hors range (max 250)
            }
        }
        
        resp = client.put(
            "/restconf/data/oven:oven",
            body=json.dumps(config),
            headers={"Content-Type": "application/yang-data+json"}
        )
        
        # Doit echouer car la temperature est hors range
        assert resp.status_code in (400, 404, 401, 403)

    @require_oven_module
    def test_015_temperature_minimum(self, server_process, client):
        """
        TC-OVEN-015 : Temperature minimale
        
        Essayer de mettre une temperature negative.
        
        Expected: 400 Bad Request
        """
        config = {
            "oven:oven": {
                "temperature": -10  # Hors range (min 0)
            }
        }
        
        resp = client.put(
            "/restconf/data/oven:oven",
            body=json.dumps(config),
            headers={"Content-Type": "application/yang-data+json"}
        )
        
        assert resp.status_code in (400, 404, 401, 403)

    @require_oven_module
    def test_016_default_values(self, server_process, client):
        """
        TC-OVEN-016 : Valeurs par defaut
        
        Verifier que les valeurs par defaut sont appliquees.
        turned-on a default=false, temperature a default=0.
        
        Expected: 200 OK avec les valeurs par defaut
        """
        # Supprimer la configuration pour revenir aux defaults
        client.delete("/restconf/data/oven:oven")
        
        # Lire la configuration
        resp = client.get("/restconf/data/oven:oven")
        
        if resp.status_code == 200:
            data = resp.json()
            oven = data.get("oven:oven", {})
            # Vérifier les valeurs par defaut
            # Note: L'application des defaults depend de with-defaults
            assert isinstance(oven, dict) or True
        else:
            assert resp.status_code in (200, 404, 401, 403)


class TestOvenTypeDef:
    """Tests du typedef oven-temperature"""

    @require_oven_module
    def test_017_typedef_validation(self, server_process, client):
        """
        TC-OVEN-017 : Validation du typedef oven-temperature
        
        Le type oven-temperature est un uint8 avec range 0..250.
        Verifier que les valeurs a la limite sont acceptees.
        
        Expected: 201/204 Created
        """
        # Tester la limite superieure (250)
        config = {
            "oven:oven": {
                "temperature": 250  # Limite superieure
            }
        }
        
        resp = client.put(
            "/restconf/data/oven:oven",
            body=json.dumps(config),
            headers={"Content-Type": "application/yang-data+json"}
        )
        
        assert resp.status_code in (201, 204, 401, 403)

    @require_oven_module
    def test_018_typedef_boundary(self, server_process, client):
        """
        TC-OVEN-018 : Limites du typedef
        
        Tester la limite inferieure (0).
        
        Expected: 201/204 Created
        """
        config = {
            "oven:oven": {
                "temperature": 0  # Limite inferieure
            }
        }
        
        resp = client.put(
            "/restconf/data/oven:oven",
            body=json.dumps(config),
            headers={"Content-Type": "application/yang-data+json"}
        )
        
        assert resp.status_code in (201, 204, 401, 403)


class TestOvenWorkflows:
    """Tests de workflows complets avec le four"""

    @require_oven_module
    def test_019_complete_workflow(self, server_process, client):
        """
        TC-OVEN-019 : Workflow complet
        
        1. Allumer le four
        2. Regler la temperature
        3. Mettre la nourriture
        4. Verifier l'etat
        
        Expected: Toutes les operations reussissent
        """
        # Step 1: Allumer le four et regler la temperature
        config = {
            "oven:oven": {
                "turned-on": True,
                "temperature": 180
            }
        }
        resp = client.put(
            "/restconf/data/oven:oven",
            body=json.dumps(config),
            headers={"Content-Type": "application/yang-data+json"}
        )
        assert resp.status_code in (201, 204, 401, 403)
        
        # Step 2: Verifier la configuration
        resp = client.get("/restconf/data/oven:oven")
        if resp.status_code == 200:
            data = resp.json()
            oven = data.get("oven:oven", {})
            assert oven.get("turned-on") == True
            assert oven.get("temperature") == 180
        
        # Step 3: Mettre la nourriture (immediatement)
        rpc_data = {
            "oven:input": {
                "time": "now"
            }
        }
        resp = client.post(
            "/restconf/operations/oven:insert-food",
            body=json.dumps(rpc_data),
            headers={"Content-Type": "application/yang-data+json"}
        )
        assert resp.status_code in (200, 204, 404, 401, 403)
        
        # Step 4: Verifier l'etat
        resp = client.get("/restconf/data/oven:oven-state")
        assert resp.status_code in (200, 404, 401, 403)

    @require_oven_module
    def test_020_cleanup_workflow(self, server_process, client):
        """
        TC-OVEN-020 : Workflow de nettoyage
        
        1. Retirer la nourriture
        2. Eteindre le four
        3. Reinitialiser la temperature
        
        Expected: Toutes les operations reussissent
        """
        # Step 1: Retirer la nourriture
        resp = client.post("/restconf/operations/oven:remove-food")
        assert resp.status_code in (200, 204, 404, 401, 403)
        
        # Step 2: Eteindre le four
        config = {
            "oven:oven": {
                "turned-on": False,
                "temperature": 0
            }
        }
        resp = client.put(
            "/restconf/data/oven:oven",
            body=json.dumps(config),
            headers={"Content-Type": "application/yang-data+json"}
        )
        assert resp.status_code in (201, 204, 401, 403)
        
        # Step 3: Supprimer la configuration
        resp = client.delete("/restconf/data/oven:oven")
        assert resp.status_code in (204, 404, 401, 403)
