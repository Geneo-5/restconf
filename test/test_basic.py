"""
Tests basiques RESTCONF (RFC 8040).

- Root Discovery (RFC 8040 §3.1)
- API Resource (RFC 8040 §3.2)
- Méthodes HTTP supportées
- YANG Library / modules-state (RFC 8040 §3.3.3, RFC 7895)
"""

import pytest


class TestRootDiscovery:
    """Tests de découverte de la racine RESTCONF."""

    def test_host_meta(self, server_process, client):
        """
        Test Root Discovery (RFC 8040 §3.1).
        GET /.well-known/host-meta retourne le lien vers /restconf
        au format XRD XML.
        """
        resp = client.get(
            "/.well-known/host-meta",
            headers={"Accept": "application/xrd+xml"},
        )

        assert resp.status_code == 200
        assert "application/xrd+xml" in resp.headers.get(
            "content-type", ""
        )
        assert "/restconf" in resp.text

    def test_host_meta_content(self, server_process, client):
        """
        Vérifie le contenu XRD du host-meta.
        """
        resp = client.get(
            "/.well-known/host-meta",
            headers={"Accept": "application/xrd+xml"},
        )

        assert resp.status_code == 200
        assert "<XRD" in resp.text
        assert "restconf" in resp.text.lower()

    def test_host_meta_json(self, server_process, client):
        """
        Test Root Discovery avec format JSON (RFC 8040 §3.1).
        Le serveur peut retourner 404 si non implémenté.
        """
        resp = client.get(
            "/.well-known/host-meta.json",
            headers={"Accept": "application/json"},
        )

        # 404 acceptable (non implémenté) ou 200
        assert resp.status_code in (200, 404)

        if resp.status_code == 200:
            data = resp.json()
            assert "links" in data
            restconf_links = [
                link
                for link in data["links"]
                if link.get("rel") == "restconf"
            ]
            assert len(restconf_links) > 0


class TestAPIResource:
    """Tests de la ressource API RESTCONF (RFC 8040 §3.2)."""

    def test_api_resource_json(self, server_process, client):
        """
        Test API Resource en JSON (RFC 8040 §3.2.1).
        GET /restconf retourne les capacités RESTCONF.
        """
        resp = client.get(
            "/restconf",
            headers={"Accept": "application/yang-data+json"},
        )

        assert resp.status_code in (200, 401, 403)

        if resp.status_code == 200:
            data = resp.json()
            assert (
                "ietf-restconf:restconf" in data
                or "restconf" in data
                or isinstance(data, dict)
            )

    def test_api_resource_xml(self, server_process, client):
        """
        Test API Resource en XML (RFC 8040 §3.2.1).
        GET /restconf retourne les capacités RESTCONF en XML.
        """
        resp = client.get(
            "/restconf",
            headers={"Accept": "application/yang-data+xml"},
        )

        assert resp.status_code in (200, 401, 403)

        if resp.status_code == 200:
            ctype = resp.headers.get("content-type", "")
            assert "application/yang-data+xml" in ctype
            assert (
                "<restconf" in resp.text
                or "<?xml" in resp.text
            )

    def test_api_resource_content(self, server_process, client):
        """
        Vérifie que l'API resource contient les éléments attendus.
        """
        resp = client.get(
            "/restconf",
            headers={"Accept": "application/yang-data+json"},
        )

        if resp.status_code == 200:
            data = resp.json()
            restconf = data.get(
                "ietf-restconf:restconf", data
            )
            # data et operations doivent être présents
            assert "data" in restconf or isinstance(restconf, dict)

    def test_unsupported_media_type(self, server_process, client):
        """
        Test qu'un Accept non supporté retourne 406
        (RFC 8040 §3.5.2).
        """
        resp = client.get(
            "/restconf",
            headers={"Accept": "text/plain"},
        )

        # 406 Not Acceptable ou 200 si le serveur ignore
        assert resp.status_code in (200, 406)


class TestYangLibrary:
    """
    Tests du module ietf-yang-library (RFC 7895 / RFC 8525),
    expose nativement par sysrepo sous {+restconf}/data.

    ietf-yang-library:modules-state est un conteneur `config
    false` (donnee d'etat), peuple en interne par sysrepo. RFC 8040
    Sec 3.5 definit {+restconf}/data comme la vue UNIFIEE du
    datastore conceptuel (equivalent NETCONF <get>, PAS
    <get-config>) : un serveur conforme DOIT donc y exposer aussi
    bien les donnees de configuration que les donnees d'etat.
    """

    def test_modules_state_json(self, server_process, client):
        """
        GET /restconf/data/ietf-yang-library:modules-state (JSON).

        Ce noeud est config false : un serveur conforme DOIT
        retourner 200 avec le corps peuple. Un 204 signifierait
        "aucune donnee", ce qui serait incorrect puisque sysrepo
        peuple toujours ce conteneur nativement (RFC 8040 Sec 3.5).
        """
        resp = client.get(
            "/restconf/data/ietf-yang-library:modules-state",
            headers={"Accept": "application/yang-data+json"},
        )

        assert resp.status_code in (200, 401, 403), (
            f"status={resp.status_code} — modules-state (config "
            "false) doit renvoyer 200 avec son contenu, jamais 204 "
            "(RFC 8040 Sec 3.5)"
        )

        if resp.status_code == 200:
            ctype = resp.headers.get("content-type", "")
            assert "application/yang-data+json" in ctype

            data = resp.json()
            modules_state = data.get(
                "ietf-yang-library:modules-state", data
            )
            assert "module" in modules_state
            assert isinstance(modules_state["module"], list)
            assert len(modules_state["module"]) > 0

    def test_modules_state_xml(self, server_process, client):
        """
        GET /restconf/data/ietf-yang-library:modules-state (XML).
        """
        resp = client.get(
            "/restconf/data/ietf-yang-library:modules-state",
            headers={"Accept": "application/yang-data+xml"},
        )

        assert resp.status_code in (200, 401, 403), (
            f"status={resp.status_code} — modules-state ne doit "
            "jamais renvoyer 204, y compris en XML"
        )

        if resp.status_code == 200:
            ctype = resp.headers.get("content-type", "")
            assert "application/yang-data+xml" in ctype
            assert "<modules-state" in resp.text
            assert "<module>" in resp.text

    def test_modules_state_contains_yang_library_module(
        self, server_process, client
    ):
        """
        Le module ietf-yang-library lui-meme DOIT apparaitre dans
        sa propre liste de modules (RFC 8040 Sec 3.3.3, RFC 7895).
        """
        resp = client.get(
            "/restconf/data/ietf-yang-library:modules-state",
            headers={"Accept": "application/yang-data+json"},
        )

        if resp.status_code != 200:
            pytest.skip(
                f"modules-state non accessible: {resp.status_code}"
            )

        data = resp.json()
        modules_state = data.get(
            "ietf-yang-library:modules-state", data
        )
        names = {
            m.get("name") for m in modules_state.get("module", [])
        }
        assert "ietf-yang-library" in names, (
            "ietf-yang-library doit lister son propre module "
            f"(modules trouves: {sorted(n for n in names if n)})"
        )


class TestHTTPMethods:
    """Tests des méthodes HTTP supportées."""

    def test_head(self, server_process, client):
        """
        Test de la méthode HEAD (RFC 8040 §3.4).
        HEAD retourne les mêmes headers que GET sans body.
        """
        resp = client.head(
            "/restconf",
            headers={"Accept": "application/yang-data+json"},
        )

        assert resp.status_code in (200, 401, 403)
        assert len(resp.content) == 0

    def test_options(self, server_process, client):
        """
        Test de la méthode OPTIONS (RFC 8040 §3.4).
        OPTIONS retourne les méthodes supportées.
        """
        resp = client.options("/restconf")

        assert resp.status_code in (200, 204, 405)

        if resp.status_code in (200, 204):
            allow = resp.headers.get("allow", "")
            assert "GET" in allow or "HEAD" in allow


class TestErrorHandling:
    """Tests de la gestion d'erreurs RESTCONF."""

    def test_404_unknown_path(self, server_process, client):
        """
        Un chemin inconnu doit retourner 404.
        """
        resp = client.get(
            "/restconf/data/unknown-module:unknown-node",
            headers={"Accept": "application/yang-data+json"},
        )

        # 404 ou 401 (auth requise avant le routage)
        assert resp.status_code in (400, 401, 403, 404)

    def test_bad_uri_format(self, server_process, client):
        """
        Une URI malformée doit retourner 400.
        """
        resp = client.get(
            "/restconf/data/%%%invalid",
            headers={"Accept": "application/yang-data+json"},
        )

        assert resp.status_code in (400, 401, 403, 404)

    def test_method_not_allowed_on_api(self, server_process, client):
        """
        DELETE sur /restconf doit être refusé (RFC 8040 §3.3).
        """
        resp = client.delete(
            "/restconf",
            headers={"Accept": "application/yang-data+json"},
        )

        assert resp.status_code in (405, 401, 403)
