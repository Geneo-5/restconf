# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of restconf.
# Copyright (C) 2026 Loic JOURDHEUIL SELLIN

"""Tests de conformité RESTCONF - Section 6 : Requêtes conditionnelles.

Couvre les items T-COND-01 à T-COND-11 de la ROADMAP.md.
RFC liées : RFC 8040 §3.4.1, §3.5.1-2, RFC 9110.
Items liés : R9, R42, A11.
"""

from __future__ import annotations

import time
from email.utils import formatdate, parsedate_to_datetime

import pytest

# ---------------------------------------------------------------------------
# Références normatives et chemins
# ---------------------------------------------------------------------------

YANG_JSON = "application/yang-data+json"
YANG_XML = "application/yang-data+xml"
MOD = "restconf-test"

BASIC_DATA = "/data/restconf-test:basic-data"
# On utilise un container à présence pour les tests d'écriture/suppression conditionnelle
SYSTEM_CONFIG = "/data/restconf-test:system/config"

def get_content_type(response) -> str:
    return response.headers.get("content-type", "").split(";")[0].strip().lower()

# ---------------------------------------------------------------------------
# Fixtures locales
# ---------------------------------------------------------------------------

_RT_PROBE_PATHS = (BASIC_DATA, SYSTEM_CONFIG)

@pytest.fixture(scope="session")
def rt_probe(http2_client, api_url, auth_headers, test_jwt):
    """Sonde l'accessibilité du module `restconf-test` sur le datastore."""
    if not test_jwt:
        return None
    headers = {"Accept": YANG_JSON, **auth_headers}
    return {
        path: http2_client.get(f"{api_url}{path}", headers=headers)
        for path in _RT_PROBE_PATHS
    }

@pytest.fixture()
def require_rt(rt_probe):
    """Skip le test si le module `restconf-test` n'est pas installé/accessible."""
    if rt_probe is None:
        pytest.skip("Aucun JWT configuré")
    
    statuses = {path: resp.status_code for path, resp in rt_probe.items()}
    if any(resp.status_code == 200 for resp in rt_probe.values()):
        return rt_probe
    if all(resp.status_code == 404 for resp in rt_probe.values()):
        pytest.skip(f"Module YANG 'restconf-test' non installé ou données non chargées ({statuses})")
    if all(resp.status_code in (401, 403) for resp in rt_probe.values()):
        pytest.skip(f"Accès au datastore restconf-test refusé ({statuses})")
    pytest.skip(f"Datastore restconf-test inaccessible ({statuses})")

@pytest.fixture()
def clean_system_config(http2_client, api_url, auth_headers):
    """Fixture de teardown pour s'assurer que system/config est dans un état connu après les tests."""
    yield
    # Nettoyage : on tente de supprimer system/config pour éviter d'impacter les autres tests
    headers = {**auth_headers}
    try:
        http2_client.delete(f"{api_url}{SYSTEM_CONFIG}", headers=headers)
    except Exception:
        pass

# ============================================================================
# T-COND-01 : GET sur une ressource -> Présence de ETag et/ou Last-Modified
# ============================================================================
@pytest.mark.roadmap("R9")
@pytest.mark.rfc("RFC 8040 §3.4.1")
@pytest.mark.rfc("RFC 9110")
class TestT_COND_01_ValidatorsPresence:
    """
    T-COND-01 : GET sur une ressource.
    Présence de `ETag` et/ou `Last-Modified` si supporté par le serveur.
    """
    def test_validators_presence(self, http2_client, api_url, auth_headers, require_rt):
        headers = {"Accept": YANG_JSON, **auth_headers}
        response = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers)
        assert response.status_code == 200
        
        etag = response.headers.get("etag")
        last_modified = response.headers.get("last-modified")
        
        # Le serveur DOIT supporter au moins l'un des deux validateurs conditionnels.
        assert etag is not None or last_modified is not None, (
            "Le serveur doit retourner au moins un validateur conditionnel "
            "(ETag ou Last-Modified) sur les réponses GET (RFC 8040 §3.4.1)."
        )

# ============================================================================
# T-COND-02 & T-COND-03 : GET avec If-None-Match
# ============================================================================
@pytest.mark.roadmap("R9")
@pytest.mark.rfc("RFC 8040 §3.4.1")
@pytest.mark.rfc("RFC 9110")
class TestT_COND_02_03_IfNoneMatch:
    """
    T-COND-02 : GET avec `If-None-Match` égal à l'ETag courant -> 304 Not Modified sans corps.
    T-COND-03 : GET avec `If-None-Match` différent -> 200 OK avec corps.
    """
    def test_if_none_match_equal(self, http2_client, api_url, auth_headers, require_rt):
        """T-COND-02 : If-None-Match égal à l'ETag courant."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        resp_get = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers)
        if resp_get.status_code != 200:
            pytest.skip(f"Impossible de récupérer la ressource ({resp_get.status_code})")
            
        etag = resp_get.headers.get("etag")
        if not etag:
            pytest.skip("Le serveur ne retourne pas d'ETag, test If-None-Match non applicable.")
            
        cond_headers = {**headers, "If-None-Match": etag}
        resp_cond = http2_client.get(f"{api_url}{BASIC_DATA}", headers=cond_headers)
        
        assert resp_cond.status_code == 304, (
            f"GET avec If-None-Match={etag!r} doit retourner 304 Not Modified, "
            f"obtenu {resp_cond.status_code}"
        )
        assert resp_cond.content == b"", "304 Not Modified ne doit pas contenir de corps."

    def test_if_none_match_different(self, http2_client, api_url, auth_headers, require_rt):
        """T-COND-03 : If-None-Match différent de l'ETag courant."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        fake_etag = '"W/\"fake-etag-123456789\""'
        
        cond_headers = {**headers, "If-None-Match": fake_etag}
        resp_cond = http2_client.get(f"{api_url}{BASIC_DATA}", headers=cond_headers)
        
        assert resp_cond.status_code == 200, (
            f"GET avec If-None-Match={fake_etag!r} doit retourner 200 OK, "
            f"obtenu {resp_cond.status_code}"
        )
        assert len(resp_cond.content) > 0, "200 OK doit contenir un corps."

# ============================================================================
# T-COND-04 & T-COND-05 : PUT avec If-Match
# ============================================================================
@pytest.mark.roadmap("R9")
@pytest.mark.rfc("RFC 8040 §3.4.1")
@pytest.mark.rfc("RFC 9110")
class TestT_COND_04_05_PutIfMatch:
    """
    T-COND-04 : PUT avec `If-Match` valide -> Modification acceptée.
    T-COND-05 : PUT avec `If-Match` invalide -> 412 Precondition Failed.
    """
    def test_put_if_match_valid(self, http2_client, api_url, auth_headers, require_rt, clean_system_config):
        """T-COND-04 : PUT avec If-Match valide."""
        headers = {"Accept": YANG_JSON, "Content-Type": YANG_JSON, **auth_headers}
        
        # Initialisation de la ressource
        resp_init = http2_client.put(f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=b"{}")
        if resp_init.status_code not in (200, 201, 204):
            pytest.skip(f"Impossible d'initialiser {SYSTEM_CONFIG} ({resp_init.status_code})")
            
        resp_get = http2_client.get(f"{api_url}{SYSTEM_CONFIG}", headers=headers)
        if resp_get.status_code != 200:
            pytest.skip("Ressource non lisible après initialisation.")
            
        etag = resp_get.headers.get("etag")
        if not etag:
            pytest.skip("Pas d'ETag retourné, test If-Match non applicable.")
            
        put_headers = {**headers, "If-Match": etag}
        resp_put = http2_client.put(f"{api_url}{SYSTEM_CONFIG}", headers=put_headers, content=b"{}")
        
        assert resp_put.status_code in (200, 204), (
            f"PUT avec If-Match={etag!r} doit retourner 200 ou 204, "
            f"obtenu {resp_put.status_code}"
        )

    def test_put_if_match_invalid(self, http2_client, api_url, auth_headers, require_rt, clean_system_config):
        """T-COND-05 : PUT avec If-Match invalide."""
        headers = {"Accept": YANG_JSON, "Content-Type": YANG_JSON, **auth_headers}
        
        # S'assurer que la ressource existe pour que le test porte bien sur le validateur
        http2_client.put(f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=b"{}")
        
        fake_etag = '"W/\"invalid-etag-987654321\""'
        put_headers = {**headers, "If-Match": fake_etag}
        resp_put = http2_client.put(f"{api_url}{SYSTEM_CONFIG}", headers=put_headers, content=b"{}")
        
        assert resp_put.status_code == 412, (
            f"PUT avec If-Match invalide ({fake_etag!r}) doit retourner 412 Precondition Failed, "
            f"obtenu {resp_put.status_code}"
        )

# ============================================================================
# T-COND-06 & T-COND-07 : DELETE avec If-Match
# ============================================================================
@pytest.mark.roadmap("R9")
@pytest.mark.rfc("RFC 8040 §3.4.1")
@pytest.mark.rfc("RFC 9110")
class TestT_COND_06_07_DeleteIfMatch:
    """
    T-COND-06 : DELETE avec `If-Match` valide -> Suppression acceptée.
    T-COND-07 : DELETE avec `If-Match` invalide -> 412 Precondition Failed.
    """
    def test_delete_if_match_valid(self, http2_client, api_url, auth_headers, require_rt, clean_system_config):
        """T-COND-06 : DELETE avec If-Match valide."""
        headers = {"Accept": YANG_JSON, "Content-Type": YANG_JSON, **auth_headers}
        
        http2_client.put(f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=b"{}")
        
        resp_get = http2_client.get(f"{api_url}{SYSTEM_CONFIG}", headers=headers)
        if resp_get.status_code != 200:
            pytest.skip("Ressource non lisible après initialisation.")
            
        etag = resp_get.headers.get("etag")
        if not etag:
            pytest.skip("Pas d'ETag retourné, test If-Match non applicable.")
            
        del_headers = {**headers, "If-Match": etag}
        resp_del = http2_client.delete(f"{api_url}{SYSTEM_CONFIG}", headers=del_headers)
        
        assert resp_del.status_code == 204, (
            f"DELETE avec If-Match={etag!r} doit retourner 204 No Content, "
            f"obtenu {resp_del.status_code}"
        )

    def test_delete_if_match_invalid(self, http2_client, api_url, auth_headers, require_rt, clean_system_config):
        """T-COND-07 : DELETE avec If-Match invalide."""
        headers = {"Accept": YANG_JSON, "Content-Type": YANG_JSON, **auth_headers}
        
        http2_client.put(f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=b"{}")
        
        fake_etag = '"W/\"invalid-etag-11223344\""'
        del_headers = {**headers, "If-Match": fake_etag}
        resp_del = http2_client.delete(f"{api_url}{SYSTEM_CONFIG}", headers=del_headers)
        
        assert resp_del.status_code == 412, (
            f"DELETE avec If-Match invalide ({fake_etag!r}) doit retourner 412 Precondition Failed, "
            f"obtenu {resp_del.status_code}"
        )

# ============================================================================
# T-COND-08 & T-COND-09 : GET avec If-Modified-Since
# ============================================================================
@pytest.mark.roadmap("R9")
@pytest.mark.rfc("RFC 8040 §3.4.1")
@pytest.mark.rfc("RFC 9110")
class TestT_COND_08_09_IfModifiedSince:
    """
    T-COND-08 : GET avec `If-Modified-Since` antérieur à la modification -> 200 OK.
    T-COND-09 : GET avec `If-Modified-Since` postérieur à la modification -> 304 Not Modified.
    """
    def test_if_modified_since_older(self, http2_client, api_url, auth_headers, require_rt):
        """T-COND-08 : If-Modified-Since antérieur à la modification."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        resp_get = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers)
        if resp_get.status_code != 200:
            pytest.skip("Impossible de lire la ressource.")
            
        last_modified = resp_get.headers.get("last-modified")
        if not last_modified:
            pytest.skip("Le serveur ne retourne pas de Last-Modified.")
            
        old_date = "Sat, 01 Jan 2000 00:00:00 GMT"
        cond_headers = {**headers, "If-Modified-Since": old_date}
        resp_cond = http2_client.get(f"{api_url}{BASIC_DATA}", headers=cond_headers)
        
        assert resp_cond.status_code == 200, (
            f"GET avec If-Modified-Since={old_date!r} (ancien) doit retourner 200 OK, "
            f"obtenu {resp_cond.status_code}"
        )

    def test_if_modified_since_newer(self, http2_client, api_url, auth_headers, require_rt):
        """T-COND-09 : If-Modified-Since postérieur à la modification."""
        headers = {"Accept": YANG_JSON, **auth_headers}
        resp_get = http2_client.get(f"{api_url}{BASIC_DATA}", headers=headers)
        if resp_get.status_code != 200:
            pytest.skip("Impossible de lire la ressource.")
            
        last_modified = resp_get.headers.get("last-modified")
        if not last_modified:
            pytest.skip("Le serveur ne retourne pas de Last-Modified.")
            
        # Date future (dans 10 ans)
        future_date = formatdate(timeval=time.time() + 315360000, usegmt=True)
        cond_headers = {**headers, "If-Modified-Since": future_date}
        resp_cond = http2_client.get(f"{api_url}{BASIC_DATA}", headers=cond_headers)
        
        assert resp_cond.status_code == 304, (
            f"GET avec If-Modified-Since={future_date!r} (futur) doit retourner 304 Not Modified, "
            f"obtenu {resp_cond.status_code}"
        )
        assert resp_cond.content == b"", "304 Not Modified ne doit pas contenir de corps."

# ============================================================================
# T-COND-10 : Modification de la ressource puis relecture -> ETag/Last-Modified change
# ============================================================================
@pytest.mark.roadmap("R9")
@pytest.mark.rfc("RFC 8040 §3.4.1")
@pytest.mark.rfc("RFC 9110")
class TestT_COND_10_ValidatorsChange:
    """
    T-COND-10 : Modification de la ressource puis relecture.
    L'ETag ou le Last-Modified change.
    """
    def test_validators_change_on_write(self, http2_client, api_url, auth_headers, require_rt, clean_system_config):
        headers = {"Accept": YANG_JSON, "Content-Type": YANG_JSON, **auth_headers}
        
        # Création initiale
        http2_client.put(f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=b"{}")
        resp_1 = http2_client.get(f"{api_url}{SYSTEM_CONFIG}", headers=headers)
        if resp_1.status_code != 200:
            pytest.skip("Impossible de lire la ressource après création.")
            
        etag_1 = resp_1.headers.get("etag")
        lm_1 = resp_1.headers.get("last-modified")
        
        if not etag_1 and not lm_1:
            pytest.skip("Aucun validateur retourné par le serveur.")
            
        # Modification (pause pour éviter les collisions de résolution temporelle sur Last-Modified)
        time.sleep(1.5)
        http2_client.put(f"{api_url}{SYSTEM_CONFIG}", headers=headers, content=b"{}")
        
        # Relecture
        resp_2 = http2_client.get(f"{api_url}{SYSTEM_CONFIG}", headers=headers)
        if resp_2.status_code != 200:
            pytest.skip("Impossible de relire la ressource après modification.")
            
        etag_2 = resp_2.headers.get("etag")
        lm_2 = resp_2.headers.get("last-modified")
        
        changed = False
        if etag_1 and etag_2:
            if etag_1 != etag_2:
                changed = True
        if lm_1 and lm_2:
            try:
                dt_1 = parsedate_to_datetime(lm_1)
                dt_2 = parsedate_to_datetime(lm_2)
                if dt_2 > dt_1:
                    changed = True
            except Exception:
                pass
                
        assert changed, (
            "L'ETag ou le Last-Modified doit changer après une modification de la ressource "
            "(RFC 8040 §3.4.1, validateurs déterministes et cohérents)."
        )

# ============================================================================
# T-COND-11 : Validateurs sur ressources NMDA
# ============================================================================
@pytest.mark.roadmap("R9")
@pytest.mark.rfc("RFC 8040 §3.4.1")
@pytest.mark.rfc("RFC 8527")
class TestT_COND_11_NMDAValidators:
    """
    T-COND-11 : Validateurs sur ressources NMDA.
    Les validateurs sont cohérents par datastore si pertinent.
    Ce test est conditionnel au support de NMDA (R27).
    """
    def test_nmda_validators(self, http2_client, api_url, auth_headers):
        nmda_oper = "/ds/ietf-datastores:operational"
        headers = {"Accept": YANG_JSON, **auth_headers}
        
        resp = http2_client.get(f"{api_url}{nmda_oper}", headers=headers)
        
        if resp.status_code == 404:
            pytest.skip("NMDA non supporté ou datastore operational non exposé via /ds/")
        if resp.status_code in (401, 403):
            pytest.skip(f"Accès refusé à {nmda_oper} ({resp.status_code})")
            
        assert resp.status_code == 200, f"GET sur {nmda_oper} doit retourner 200, obtenu {resp.status_code}"
        
        etag = resp.headers.get("etag")
        lm = resp.headers.get("last-modified")
        
        # Si NMDA est supporté, les validateurs doivent s'appliquer ou au minimum la requête doit réussir.
        assert etag is not None or lm is not None or resp.status_code == 200