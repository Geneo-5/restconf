# Campagnes de qualification

## ⚠️ Prérequis Module YANG

**Toutes les campagnes DOIVENT vérifier que le module `restconf-test.yang` (namespace: `urn:restconf:test`, prefix: `rt`) est chargé** avant d'exécuter les tests.

Vérification : `GET /restconf/data/rt:restconf-test` doit retourner HTTP 200.

---

Toutes les campagnes ne rejouent pas les 350 cas de test.

Les campagnes sont organisées par objectifs et utilisent principalement le module `restconf-test.yang`.

---

# Smoke Test

**Durée** : ≈ 5 minutes

**Objectif** : Valider que le serveur est disponible et que le module `restconf-test.yang` est chargé.

**Tests** :
- RESTCONF-INF-* (infrastructure de base)
- RESTCONF-CRUD-001 (lecture root /data)
- RESTCONF-CRUD-002 (lecture container rt:restconf-test)
- RESTCONF-RPC-001 (découverte des RPC rt:*)

---

# Sanity Test

Durée

≈ 20 minutes

Objectif

Valider les fonctionnalités principales.

Tests

Infrastructure

CRUD

RPC

Notifications

---

# Qualification

Durée

≈ 3 heures

Tous les cas de tests.

---

# Régression

Durée

≈ 4 heures

Tous les cas ayant déjà détecté un défaut.

+

Toutes les fonctionnalités modifiées.

---

# Performance

Durée

≈ 8 heures

Charge

Endurance

Concurrence

Mémoire

CPU

---

# Sécurité

Durée

≈ 2 heures

TLS

NACM

Injection

Authentification

Autorisation

---

# Certification RFC

Durée

≈ 1 journée

Tous les tests référencés
dans la matrice RFC.