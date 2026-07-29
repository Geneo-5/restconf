"""
Tests de conformité pour la section "3. Ressource racine API et capacités" de la ROADMAP.

Items liés : R4, R15, R40, R45
RFC liées : RFC 8040 §3.3, §3.6, §3.8, §9.1

Comportements normatifs testés (basés sur les RFC, pas sur l'implémentation) :

RFC 8040 §3.3 - API Resource :
  - GET sur {+restconf} retourne la ressource racine API
  - La réponse contient les sous-ressources : data, operations, yang-library-version
  - Media types : application/yang-data+json ou application/yang-data+xml
  - En JSON : {"ietf-restconf:restconf":{"data":{},"operations":{},"yang-library-version":"..."}}
  - En XML : <restconf xmlns="urn:ietf:params:xml:ns:yang:ietf-restconf"><data/><operations/><yang-library-version>...</yang-library-version></restconf>

RFC 8040 §3.6 - Operation Resource :
  - GET sur {+restconf}/operations retourne la liste des RPC disponibles
  - OPTIONS sur {+restconf}/operations doit retourner Allow

RFC 8040 §9.1 - restconf-state/capabilities :
  - GET sur {+restconf}/data/ietf-restconf-monitoring:restconf-state/capabilities
  - Retourne la liste des capacités RESTCONF supportées
  - Capacités standard : defaults, with-defaults, depth, fields, filter, start-time, stop-time
  - Format : urn:ietf:params:restconf:capability:<name>:1.0

RFC 9110 - HTTP Semantics :
  - OPTIONS doit retourner un header Allow
  - 401 Unauthorized doit inclure WWW-Authenticate
  - 403 Forbidden pour authentification valide mais accès refusé
"""

import pytest
import httpx
import xml.etree.ElementTree as ET
import json
import re

# Namespaces XML
RESTCONF_NS = "urn:ietf:params:xml:ns:yang:ietf-restconf"
RESTCONF_MONITORING_NS = "urn:ietf:params:xml:ns:yang:ietf-restconf-monitoring"

# URN des capacités RESTCONF standard (RFC 8040 §9.1.1, §11.4)
CAPABILITY_URNS = {
    "defaults": "urn:ietf:params:restconf:capability:defaults:1.0",
    "with-defaults": "urn:ietf:params:restconf:capability:with-defaults:1.0",
    "depth": "urn:ietf:params:restconf:capability:depth:1.0",
    "fields": "urn:ietf:params:restconf:capability:fields:1.0",
    "filter": "urn:ietf:params:restconf:capability:filter:1.0",
    "start-time": "urn:ietf:params:restconf:capability:start-time:1.0",
    "stop-time": "urn:ietf:params:restconf:capability:stop-time:1.0",
}

# ============================================================================
# T-API-01 : GET sur {+restconf}
# ============================================================================


@pytest.mark.roadmap("R4")
@pytest.mark.rfc("RFC 8040 §3.3")
class TestAPIResourceRetrieval:
    """T-API-01 : GET sur {+restconf} retourne la ressource racine API."""

    def test_get_api_root_json(self, http2_client, api_url, auth_headers, require_jwt):
        """
        RFC 8040 §3.3, B.1.1 : GET sur {+restconf} avec Accept: application/yang-data+json
        doit retourner la ressource racine API en JSON.
        
        Réponse attendue (exemple RFC 8040 B.1.1) :
        {
          "ietf-restconf:restconf": {
            "data": {},
            "operations": {},
            "yang-library-version": "2016-06-21"
          }
        }
        """
        headers = {
            "Accept": "application/yang-data+json",
            **auth_headers,
        }
        response = http2_client.get(api_url, headers=headers)
        
        # Code de succès
        assert response.status_code == 200, (
            f"GET {api_url} doit retourner 200 OK, "
            f"reçu {response.status_code}"
        )
        
        # Content-Type
        content_type = response.headers.get("content-type", "")
        assert "application/yang-data+json" in content_type, (
            f"Content-Type doit être application/yang-data+json, "
            f"reçu {content_type}"
        )
        
        # Corps JSON valide
        body = response.json()
        assert "ietf-restconf:restconf" in body, (
            "La réponse JSON doit contenir 'ietf-restconf:restconf'"
        )
        
        restconf_obj = body["ietf-restconf:restconf"]
        
        # Sous-ressources obligatoires
        assert "data" in restconf_obj, (
            "La ressource racine doit contenir 'data'"
        )
        assert "operations" in restconf_obj, (
            "La ressource racine doit contenir 'operations'"
        )
        
        # yang-library-version (RFC 8040 §3.3.3)
        # Note : peut être absent si YANG Library n'est pas supportée
        # mais si présent, doit être une chaîne de révision YANG
        if "yang-library-version" in restconf_obj:
            version = restconf_obj["yang-library-version"]
            assert isinstance(version, str), (
                "yang-library-version doit être une chaîne"
            )
            # Format de révision YANG : YYYY-MM-DD
            assert re.match(r"^\d{4}-\d{2}-\d{2}$", version), (
                f"yang-library-version doit être au format YYYY-MM-DD, "
                f"reçu {version}"
            )

    def test_get_api_root_xml(self, http2_client, api_url, auth_headers, require_jwt):
        """
        RFC 8040 §3.3, B.1.1 : GET sur {+restconf} avec Accept: application/yang-data+xml
        doit retourner la ressource racine API en XML.
        
        Réponse attendue (exemple RFC 8040 B.1.1) :
        <restconf xmlns="urn:ietf:params:xml:ns:yang:ietf-restconf">
          <data/>
          <operations/>
          <yang-library-version>2016-06-21</yang-library-version>
        </restconf>
        """
        headers = {
            "Accept": "application/yang-data+xml",
            **auth_headers,
        }
        response = http2_client.get(api_url, headers=headers)
        
        # Code de succès
        assert response.status_code == 200, (
            f"GET {api_url} doit retourner 200 OK, "
            f"reçu {response.status_code}"
        )
        
        # Content-Type
        content_type = response.headers.get("content-type", "")
        assert "application/yang-data+xml" in content_type, (
            f"Content-Type doit être application/yang-data+xml, "
            f"reçu {content_type}"
        )
        
        # Corps XML valide
        root = ET.fromstring(response.text)
        
        # Namespace
        assert root.tag == f"{{{RESTCONF_NS}}}restconf", (
            f"L'élément racine doit être {{urn:ietf:params:xml:ns:yang:ietf-restconf}}restconf, "
            f"reçu {root.tag}"
        )
        
        # Sous-ressources obligatoires
        data_elem = root.find(f"{{{RESTCONF_NS}}}data")
        assert data_elem is not None, (
            "La ressource racine doit contenir <data/>"
        )
        
        operations_elem = root.find(f"{{{RESTCONF_NS}}}operations")
        assert operations_elem is not None, (
            "La ressource racine doit contenir <operations/>"
        )
        
        # yang-library-version (optionnel mais si présent, format YYYY-MM-DD)
        version_elem = root.find(f"{{{RESTCONF_NS}}}yang-library-version")
        if version_elem is not None:
            version = version_elem.text
            assert version is not None, (
                "yang-library-version ne doit pas être vide"
            )
            assert re.match(r"^\d{4}-\d{2}-\d{2}$", version), (
                f"yang-library-version doit être au format YYYY-MM-DD, "
                f"reçu {version}"
            )


# ============================================================================
# T-API-02 : Vérification des sous-ressources annoncées
# ============================================================================


@pytest.mark.roadmap("R4")
@pytest.mark.rfc("RFC 8040 §3.3")
class TestAPISubResources:
    """T-API-02 : Vérification des sous-ressources annoncées."""

    def test_data_subresource_accessible(self, http2_client, api_url, auth_headers):
        """
        RFC 8040 §3.3.1 : La sous-ressource {+restconf}/data doit être accessible.
        """
        headers = {
            "Accept": "application/yang-data+json",
            **auth_headers,
        }
        response = http2_client.get(f"{api_url}/data", headers=headers)
        
        # Doit être accessible (200) ou refusé (401/403) mais pas 404
        assert response.status_code in [200, 401, 403], (
            f"GET {api_url}/data doit retourner 200, 401 ou 403, "
            f"reçu {response.status_code}"
        )
        
        if response.status_code == 200:
            body = response.json()
            # La réponse doit contenir un objet data
            assert "ietf-restconf:data" in body or "data" in body, (
                "La réponse de /data doit contenir 'ietf-restconf:data' ou 'data'"
            )

    def test_operations_subresource_accessible(self, http2_client, api_url, auth_headers):
        """
        RFC 8040 §3.3.2, §3.6 : La sous-ressource {+restconf}/operations doit être accessible.
        """
        headers = {
            "Accept": "application/yang-data+json",
            **auth_headers,
        }
        response = http2_client.get(f"{api_url}/operations", headers=headers)
        
        # Doit être accessible (200) ou refusé (401/403) mais pas 404
        assert response.status_code in [200, 401, 403], (
            f"GET {api_url}/operations doit retourner 200, 401 ou 403, "
            f"reçu {response.status_code}"
        )
        
        if response.status_code == 200:
            body = response.json()
            # La réponse doit contenir un objet operations
            assert "operations" in body, (
                "La réponse de /operations doit contenir 'operations'"
            )

    def test_restconf_state_subresource_if_supported(
        self, http2_client, api_url, auth_headers, require_jwt
    ):
        """
        RFC 8040 §9 : La sous-ressource restconf-state peut être exposée
        via {+restconf}/data/ietf-restconf-monitoring:restconf-state.
        
        Note : restconf-state est optionnel mais recommandé pour le monitoring.
        """
        headers = {
            "Accept": "application/yang-data+json",
            **auth_headers,
        }
        response = http2_client.get(
            f"{api_url}/data/ietf-restconf-monitoring:restconf-state",
            headers=headers,
        )
        
        # Peut être 200 (supporté), 404 (non supporté), 401/403 (accès refusé)
        assert response.status_code in [200, 401, 403, 404], (
            f"GET restconf-state doit retourner 200, 401, 403 ou 404, "
            f"reçu {response.status_code}"
        )
        
        if response.status_code == 200:
            body = response.json()
            # Doit contenir restconf-state
            assert "ietf-restconf-monitoring:restconf-state" in body, (
                "La réponse doit contenir 'ietf-restconf-monitoring:restconf-state'"
            )


# ============================================================================
# T-API-03 : OPTIONS sur la racine
# ============================================================================


@pytest.mark.roadmap("R40")
@pytest.mark.rfc("RFC 8040 §4.1", "RFC 9110 §9.3.7")
class TestAPIOptions:
    """T-API-03 : OPTIONS sur la racine retourne Allow."""

    def test_options_api_root(self, http2_client, api_url, auth_headers, require_jwt):
        """
        RFC 8040 §4.1, RFC 9110 §9.3.7 : OPTIONS sur {+restconf} doit retourner
        un header Allow contenant au minimum GET et OPTIONS.
        """
        headers = {**auth_headers}
        response = http2_client.options(api_url, headers=headers)
        
        # OPTIONS doit réussir
        assert response.status_code in [200, 204], (
            f"OPTIONS {api_url} doit retourner 200 ou 204, "
            f"reçu {response.status_code}"
        )
        
        # Header Allow obligatoire
        allow = response.headers.get("allow", "")
        assert allow, (
            "OPTIONS doit retourner un header Allow"
        )
        
        # Méthodes minimales
        allowed_methods = {m.strip().upper() for m in allow.split(",")}
        assert "GET" in allowed_methods, (
            f"Allow doit contenir GET, reçu {allow}"
        )
        assert "OPTIONS" in allowed_methods, (
            f"Allow doit contenir OPTIONS, reçu {allow}"
        )

    def test_options_operations(self, http2_client, api_url, auth_headers, require_jwt):
        """
        RFC 8040 §3.6, §4.1 : OPTIONS sur {+restconf}/operations doit retourner
        un header Allow contenant GET, OPTIONS et POST.
        """
        headers = {**auth_headers}
        response = http2_client.options(f"{api_url}/operations", headers=headers)
        
        # OPTIONS doit réussir
        assert response.status_code in [200, 204], (
            f"OPTIONS {api_url}/operations doit retourner 200 ou 204, "
            f"reçu {response.status_code}"
        )
        
        # Header Allow obligatoire
        allow = response.headers.get("allow", "")
        assert allow, (
            "OPTIONS doit retourner un header Allow"
        )
        
        # Méthodes attendues pour operations
        allowed_methods = {m.strip().upper() for m in allow.split(",")}
        assert "GET" in allowed_methods, (
            f"Allow pour /operations doit contenir GET, reçu {allow}"
        )
        assert "OPTIONS" in allowed_methods, (
            f"Allow pour /operations doit contenir OPTIONS, reçu {allow}"
        )
        # POST est attendu pour invoquer les RPC
        assert "POST" in allowed_methods, (
            f"Allow pour /operations doit contenir POST, reçu {allow}"
        )


# ============================================================================
# T-API-04 : GET sur restconf-state/capabilities
# ============================================================================


@pytest.mark.roadmap("R45")
@pytest.mark.rfc("RFC 8040 §9.1")
class TestCapabilitiesRetrieval:
    """T-API-04 : GET sur restconf-state/capabilities."""

    def test_get_capabilities_json(self, http2_client, api_url, auth_headers):
        """
        RFC 8040 §9.1, B.1.3 : GET sur
        {+restconf}/data/ietf-restconf-monitoring:restconf-state/capabilities
        avec Accept: application/yang-data+json doit retourner les capacités.
        
        Réponse attendue (exemple RFC 8040 B.1.3 adapté en JSON) :
        {
          "ietf-restconf-monitoring:capabilities": {
            "capability": [
              "urn:ietf:params:restconf:capability:defaults:1.0?basic-mode=explicit",
              "urn:ietf:params:restconf:capability:with-defaults:1.0",
              ...
            ]
          }
        }
        """
        headers = {
            "Accept": "application/yang-data+json",
            **auth_headers,
        }
        response = http2_client.get(
            f"{api_url}/data/ietf-restconf-monitoring:restconf-state/capabilities",
            headers=headers,
        )
        
        # Peut être 200 (supporté), 404 (non supporté), 401/403 (accès refusé)
        if response.status_code == 404:
            pytest.skip("restconf-state/capabilities non supporté")
        
        assert response.status_code in [200, 401, 403], (
            f"GET capabilities doit retourner 200, 401, 403 ou 404, "
            f"reçu {response.status_code}"
        )
        
        if response.status_code == 200:
            # Content-Type
            content_type = response.headers.get("content-type", "")
            assert "application/yang-data+json" in content_type, (
                f"Content-Type doit être application/yang-data+json, "
                f"reçu {content_type}"
            )
            
            # Corps JSON valide
            body = response.json()
            assert "ietf-restconf-monitoring:capabilities" in body, (
                "La réponse doit contenir 'ietf-restconf-monitoring:capabilities'"
            )
            
            capabilities_obj = body["ietf-restconf-monitoring:capabilities"]
            assert "capability" in capabilities_obj, (
                "capabilities doit contenir 'capability'"
            )
            
            capability_list = capabilities_obj["capability"]
            assert isinstance(capability_list, list), (
                "capability doit être une liste"
            )
            
            # Chaque capacité doit être une URN valide
            for cap in capability_list:
                assert isinstance(cap, str), (
                    f"Chaque capacité doit être une chaîne, reçu {type(cap)}"
                )
                # Format URN ou URL
                assert cap.startswith("urn:") or cap.startswith("http"), (
                    f"Capacité doit être une URN ou URL, reçu {cap}"
                )

    def test_get_capabilities_xml(self, http2_client, api_url, auth_headers):
        """
        RFC 8040 §9.1, B.1.3 : GET sur
        {+restconf}/data/ietf-restconf-monitoring:restconf-state/capabilities
        avec Accept: application/yang-data+xml doit retourner les capacités en XML.
        
        Réponse attendue (exemple RFC 8040 B.1.3) :
        <capabilities xmlns="urn:ietf:params:xml:ns:yang:ietf-restconf-monitoring">
          <capability>urn:ietf:params:restconf:capability:defaults:1.0?basic-mode=explicit</capability>
          <capability>urn:ietf:params:restconf:capability:with-defaults:1.0</capability>
          ...
        </capabilities>
        """
        headers = {
            "Accept": "application/yang-data+xml",
            **auth_headers,
        }
        response = http2_client.get(
            f"{api_url}/data/ietf-restconf-monitoring:restconf-state/capabilities",
            headers=headers,
        )
        
        # Peut être 200 (supporté), 404 (non supporté), 401/403 (accès refusé)
        if response.status_code == 404:
            pytest.skip("restconf-state/capabilities non supporté")
        
        assert response.status_code in [200, 401, 403], (
            f"GET capabilities doit retourner 200, 401, 403 ou 404, "
            f"reçu {response.status_code}"
        )
        
        if response.status_code == 200:
            # Content-Type
            content_type = response.headers.get("content-type", "")
            assert "application/yang-data+xml" in content_type, (
                f"Content-Type doit être application/yang-data+xml, "
                f"reçu {content_type}"
            )
            
            # Corps XML valide
            root = ET.fromstring(response.text)
            
            # Namespace
            assert root.tag == f"{{{RESTCONF_MONITORING_NS}}}capabilities", (
                f"L'élément racine doit être "
                f"{{urn:ietf:params:xml:ns:yang:ietf-restconf-monitoring}}capabilities, "
                f"reçu {root.tag}"
            )
            
            # Liste des capacités
            capability_elems = root.findall(f"{{{RESTCONF_MONITORING_NS}}}capability")
            
            # Chaque capacité doit être une URN valide
            for cap_elem in capability_elems:
                cap = cap_elem.text
                assert cap is not None, (
                    "Chaque <capability> doit avoir un contenu texte"
                )
                assert cap.startswith("urn:") or cap.startswith("http"), (
                    f"Capacité doit être une URN ou URL, reçu {cap}"
                )


# ============================================================================
# T-API-05 : Cohérence des capacités annoncées
# ============================================================================


@pytest.mark.roadmap("R45")
@pytest.mark.rfc("RFC 8040 §9.1.1")
class TestCapabilitiesConsistency:
    """T-API-05 : Cohérence des capacités annoncées."""

    def test_capabilities_format(self, http2_client, api_url, auth_headers):
        """
        RFC 8040 §9.1.1, §11.4 : Les capacités RESTCONF standard doivent suivre
        le format urn:ietf:params:restconf:capability:<name>:1.0
        
        Capacités standard définies :
        - defaults (avec paramètre basic-mode)
        - with-defaults
        - depth
        - fields
        - filter
        - start-time
        - stop-time
        """
        headers = {
            "Accept": "application/yang-data+json",
            **auth_headers,
        }
        response = http2_client.get(
            f"{api_url}/data/ietf-restconf-monitoring:restconf-state/capabilities",
            headers=headers,
        )
        
        if response.status_code == 404:
            pytest.skip("restconf-state/capabilities non supporté")
        
        if response.status_code != 200:
            pytest.skip(f"Accès refusé ({response.status_code})")
        
        body = response.json()
        capabilities = body.get("ietf-restconf-monitoring:capabilities", {}).get(
            "capability", []
        )
        
        # Vérifier le format des capacités standard
        for cap in capabilities:
            if cap.startswith("urn:ietf:params:restconf:capability:"):
                # Extraire le nom de la capacité
                match = re.match(
                    r"urn:ietf:params:restconf:capability:([^:]+):(\d+\.\d+)(\?.*)?$",
                    cap,
                )
                assert match, (
                    f"Capacité standard mal formée : {cap}. "
                    f"Format attendu : urn:ietf:params:restconf:capability:<name>:<version>[?params]"
                )
                
                cap_name = match.group(1)
                cap_version = match.group(2)
                
                # Version doit être 1.0 pour les capacités standard
                assert cap_version == "1.0", (
                    f"Capacité {cap_name} doit avoir la version 1.0, "
                    f"reçu {cap_version}"
                )
                
                # Si c'est une capacité standard connue, vérifier le nom
                known_names = set(CAPABILITY_URNS.keys())
                if cap_name in known_names:
                    # Vérifier que l'URN correspond
                    expected_urn = CAPABILITY_URNS[cap_name]
                    assert cap.startswith(expected_urn), (
                        f"Capacité {cap_name} doit commencer par {expected_urn}, "
                        f"reçu {cap}"
                    )

    def test_defaults_capability_has_basic_mode(
        self, http2_client, api_url, auth_headers, require_jwt
    ):
        """
        RFC 8040 §9.1.2 : Si la capacité 'defaults' est annoncée, elle doit
        inclure le paramètre basic-mode avec une valeur valide.
        
        Valeurs valides pour basic-mode : report-all, trim, explicit
        """
        headers = {
            "Accept": "application/yang-data+json",
            **auth_headers,
        }
        response = http2_client.get(
            f"{api_url}/data/ietf-restconf-monitoring:restconf-state/capabilities",
            headers=headers,
        )
        
        if response.status_code == 404:
            pytest.skip("restconf-state/capabilities non supporté")
        
        if response.status_code != 200:
            pytest.skip(f"Accès refusé ({response.status_code})")
        
        body = response.json()
        capabilities = body.get("ietf-restconf-monitoring:capabilities", {}).get(
            "capability", []
        )
        
        # Chercher la capacité defaults
        defaults_cap = None
        for cap in capabilities:
            if cap.startswith(CAPABILITY_URNS["defaults"]):
                defaults_cap = cap
                break
        
        if defaults_cap is None:
            pytest.skip("Capacité 'defaults' non annoncée")
        
        # Doit contenir basic-mode
        assert "basic-mode=" in defaults_cap, (
            f"Capacité 'defaults' doit inclure basic-mode, reçu {defaults_cap}"
        )
        
        # Extraire la valeur de basic-mode
        match = re.search(r"basic-mode=([^&]+)", defaults_cap)
        assert match, (
            f"Impossible d'extraire basic-mode de {defaults_cap}"
        )
        
        basic_mode = match.group(1)
        valid_modes = {"report-all", "trim", "explicit"}
        assert basic_mode in valid_modes, (
            f"basic-mode doit être l'un de {valid_modes}, reçu {basic_mode}"
        )


# ============================================================================
# T-API-06 : Accès non autorisé à la racine
# ============================================================================


@pytest.mark.roadmap("R4", "R29")
@pytest.mark.rfc("RFC 8040 §2.5", "RFC 9110 §11.6")
class TestAPIAuthentication:
    """T-API-06 : Accès non autorisé à la racine si authentification requise."""

    def test_unauthenticated_access_if_auth_required(
        self, http2_client, api_url
    ):
        """
        RFC 8040 §2.5, RFC 9110 §11.6 : Si l'authentification est requise,
        une requête sans credentials doit retourner 401 Unauthorized avec
        WWW-Authenticate, ou 403 Forbidden.
        
        Note : Ce test n'envoie volontairement pas de JWT.
        """
        headers = {
            "Accept": "application/yang-data+json",
        }
        response = http2_client.get(api_url, headers=headers)
        
        # Si l'authentification est requise : 401 ou 403
        # Si l'authentification n'est pas requise : 200
        assert response.status_code in [200, 401, 403], (
            f"GET {api_url} sans authentification doit retourner 200, 401 ou 403, "
            f"reçu {response.status_code}"
        )
        
        if response.status_code == 401:
            # WWW-Authenticate obligatoire pour 401
            www_auth = response.headers.get("www-authenticate", "")
            assert www_auth, (
                "401 Unauthorized doit inclure WWW-Authenticate"
            )
            # Scheme attendu : Bearer (RFC 6750)
            assert "Bearer" in www_auth or "bearer" in www_auth.lower(), (
                f"WWW-Authenticate doit contenir 'Bearer', reçu {www_auth}"
            )
        
        if response.status_code == 403:
            # 403 peut inclure WWW-Authenticate mais ce n'est pas obligatoire
            # Le corps doit contenir une erreur RESTCONF
            content_type = response.headers.get("content-type", "")
            if "application/yang-data+json" in content_type:
                body = response.json()
                assert "ietf-restconf:errors" in body, (
                    "403 doit contenir une erreur RESTCONF"
                )

    def test_invalid_token_rejected(self, http2_client, api_url):
        """
        RFC 8040 §2.5, RFC 7519 : Un token JWT invalide doit être rejeté
        avec 401 Unauthorized.
        """
        headers = {
            "Accept": "application/yang-data+json",
            "Authorization": "Bearer invalid.token.here",
        }
        response = http2_client.get(api_url, headers=headers)
        
        # Token invalide doit être rejeté
        assert response.status_code in [401, 403], (
            f"GET avec token invalide doit retourner 401 ou 403, "
            f"reçu {response.status_code}"
        )
        
        if response.status_code == 401:
            # WWW-Authenticate obligatoire
            www_auth = response.headers.get("www-authenticate", "")
            assert www_auth, (
                "401 Unauthorized doit inclure WWW-Authenticate"
            )


# ============================================================================
# Tests complémentaires R4, R15, R40
# ============================================================================


@pytest.mark.roadmap("R4", "R15")
@pytest.mark.rfc("RFC 8040 §3.6")
class TestOperationsResource:
    """Tests complémentaires pour la ressource operations."""

    def test_get_operations_returns_rpc_list(
        self, http2_client, api_url, auth_headers, require_jwt
    ):
        """
        RFC 8040 §3.6, B.1.1 : GET sur {+restconf}/operations retourne
        la liste des RPC disponibles.
        
        Réponse attendue (exemple RFC 8040) :
        {
          "operations": {
            "example-jukebox:play": [null]
          }
        }
        """
        headers = {
            "Accept": "application/yang-data+json",
            **auth_headers,
        }
        response = http2_client.get(f"{api_url}/operations", headers=headers)
        
        if response.status_code in [401, 403]:
            pytest.skip(f"Accès refusé ({response.status_code})")
        
        assert response.status_code == 200, (
            f"GET {api_url}/operations doit retourner 200, "
            f"reçu {response.status_code}"
        )
        
        body = response.json()
        assert "operations" in body, (
            "La réponse doit contenir 'operations'"
        )
        
        operations = body["operations"]
        assert isinstance(operations, dict), (
            "operations doit être un objet JSON"
        )
        
        # Chaque RPC doit être identifié par module:rpc-name
        for rpc_name in operations.keys():
            # Format attendu : module-name:rpc-name
            assert ":" in rpc_name, (
                f"RPC '{rpc_name}' doit être au format module:rpc-name"
            )

    def test_operations_content_type_negotiation(
        self, http2_client, api_url, auth_headers, require_jwt
    ):
        """
        RFC 8040 §3.2, R41 : La ressource operations doit supporter
        la négociation de contenu JSON et XML.
        """
        # JSON
        headers_json = {
            "Accept": "application/yang-data+json",
            **auth_headers,
        }
        response_json = http2_client.get(
            f"{api_url}/operations", headers=headers_json
        )
        
        if response_json.status_code == 200:
            content_type = response_json.headers.get("content-type", "")
            assert "application/yang-data+json" in content_type, (
                f"Content-Type JSON attendu, reçu {content_type}"
            )
        
        # XML
        headers_xml = {
            "Accept": "application/yang-data+xml",
            **auth_headers,
        }
        response_xml = http2_client.get(
            f"{api_url}/operations", headers=headers_xml
        )
        
        if response_xml.status_code == 200:
            content_type = response_xml.headers.get("content-type", "")
            assert "application/yang-data+xml" in content_type, (
                f"Content-Type XML attendu, reçu {content_type}"
            )


@pytest.mark.roadmap("R40")
@pytest.mark.rfc("RFC 9110 §9.3.7", "RFC 8040 §4.1")
class TestMethodNotAllowed:
    """Tests pour 405 Method Not Allowed avec Allow."""

    def test_post_on_api_root_returns_405_with_allow(
        self, http2_client, api_url, auth_headers, require_jwt
    ):
        """
        RFC 9110 §9.3.7, RFC 8040 §4.1 : POST sur {+restconf} doit retourner
        405 Method Not Allowed avec header Allow.
        """
        headers = {
            "Accept": "application/yang-data+json",
            **auth_headers,
        }
        response = http2_client.post(api_url, headers=headers)
        
        # POST sur la racine API n'est pas autorisé
        assert response.status_code == 405, (
            f"POST {api_url} doit retourner 405, "
            f"reçu {response.status_code}"
        )
        
        # Header Allow obligatoire pour 405
        allow = response.headers.get("allow", "")
        assert allow, (
            "405 Method Not Allowed doit inclure Allow"
        )
        
        # GET et OPTIONS doivent être autorisés
        allowed_methods = {m.strip().upper() for m in allow.split(",")}
        assert "GET" in allowed_methods, (
            f"Allow doit contenir GET, reçu {allow}"
        )
        assert "OPTIONS" in allowed_methods, (
            f"Allow doit contenir OPTIONS, reçu {allow}"
        )

    def test_delete_on_api_root_returns_405_with_allow(
        self, http2_client, api_url, auth_headers, require_jwt
    ):
        """
        RFC 9110 §9.3.7, RFC 8040 §4.1 : DELETE sur {+restconf} doit retourner
        405 Method Not Allowed avec header Allow.
        """
        headers = {
            "Accept": "application/yang-data+json",
            **auth_headers,
        }
        response = http2_client.delete(api_url, headers=headers)
        
        # DELETE sur la racine API n'est pas autorisé
        assert response.status_code == 405, (
            f"DELETE {api_url} doit retourner 405, "
            f"reçu {response.status_code}"
        )
        
        # Header Allow obligatoire pour 405
        allow = response.headers.get("allow", "")
        assert allow, (
            "405 Method Not Allowed doit inclure Allow"
        )
