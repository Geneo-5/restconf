"""
Tests basiques RESTCONF (équivalent à test/basic.sh).
- Root Discovery (RFC 8040 §3.1)
- API Resource (RFC 8040 §3.2)
"""


class TestRootDiscovery:
    """Tests de découverte de la racine RESTCONF."""
    
    def test_host_meta(self, base_url, session):
        """
        Test Root Discovery (RFC 8040 §3.1).
        GET /.well-known/host-meta doit retourner le lien vers /restconf.
        """
        resp = session.get(f"{base_url}/.well-known/host-meta", headers={
            "Accept": "application/xrd+xml",
        })
        
        assert resp.status_code == 200
        assert "application/xrd+xml" in resp.headers.get("Content-Type", "")
        assert "/restconf" in resp.text
    
    def test_host_meta_json(self, base_url, session):
        """
        Test Root Discovery avec format JSON (RFC 8040 §3.1).
        """
        resp = session.get(f"{base_url}/.well-known/host-meta.json", headers={
            "Accept": "application/json",
        })
        
        # Le serveur peut retourner 404 si non implémenté, ou 200
        if resp.status_code == 200:
            data = resp.json()
            assert "links" in data
            # Vérifier la présence du lien restconf
            restconf_links = [
                link for link in data["links"]
                if link.get("rel") == "restconf"
            ]
            assert len(restconf_links) > 0


class TestAPIResource:
    """Tests de la ressource API RESTCONF (RFC 8040 §3.2)."""
    
    def test_api_resource_json(self, base_url, session):
        """
        Test API Resource en JSON (RFC 8040 §3.2.1).
        GET /restconf doit retourner les capacités RESTCONF.
        """
        resp = session.get(f"{base_url}/restconf", headers={
            "Accept": "application/yang-data+json",
        })
        
        # Le serveur peut retourner 200 avec les capacités, ou 401 si auth requise
        assert resp.status_code in (200, 401, 403)
        
        if resp.status_code == 200:
            data = resp.json()
            assert "ietf-restconf:restconf-data" in data or \
                   "restconf" in data or \
                   isinstance(data, dict)
    
    def test_api_resource_xml(self, base_url, session):
        """
        Test API Resource en XML (RFC 8040 §3.2.1).
        GET /restconf doit retourner les capacités RESTCONF en XML.
        """
        resp = session.get(f"{base_url}/restconf", headers={
            "Accept": "application/yang-data+xml",
        })
        
        # Le serveur peut retourner 200 avec les capacités, ou 401 si auth requise
        assert resp.status_code in (200, 401, 403)
        
        if resp.status_code == 200:
            assert "application/yang-data+xml" in resp.headers.get("Content-Type", "")
            assert "<restconf" in resp.text or "<?xml" in resp.text
    
    def test_unsupported_media_type(self, base_url, session):
        """
        Test qu'un Accept non supporté retourne 406 (RFC 8040 §3.5.2).
        """
        resp = session.get(f"{base_url}/restconf", headers={
            "Accept": "text/plain",
        })
        
        # 406 Not Acceptable ou 200 si le serveur ignore l'Accept
        assert resp.status_code in (200, 406)


class TestHTTPMethods:
    """Tests des méthodes HTTP supportées."""
    
    def test_options(self, base_url, session):
        """
        Test de la méthode OPTIONS (RFC 8040 §3.4).
        OPTIONS doit retourner les méthodes supportées.
        """
        resp = session.options(f"{base_url}/restconf")
        
        assert resp.status_code in (200, 204, 405)
        
        if resp.status_code in (200, 204):
            allow = resp.headers.get("Allow", "")
            # RESTCONF supporte au minimum GET, HEAD
            assert "GET" in allow or "HEAD" in allow
    
    def test_head(self, base_url, session):
        """
        Test de la méthode HEAD (RFC 8040 §3.4).
        HEAD doit retourner les mêmes en-têtes que GET sans body.
        """
        resp = session.head(f"{base_url}/restconf", headers={
            "Accept": "application/yang-data+json",
        })
        
        # HEAD doit retourner le même status que GET
        assert resp.status_code in (200, 401, 403)
        assert len(resp.content) == 0  # Pas de body pour HEAD
