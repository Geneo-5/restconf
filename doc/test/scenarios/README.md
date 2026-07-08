# Scénarios End-to-End

## ⚠️ Prérequis Module YANG

**Tous les scénarios de ce document DOIVENT utiliser le module `restconf-test.yang`** (namespace: `urn:restconf:test`, prefix: `rt`).

Vérifier que le module est chargé avant d'exécuter les scénarios :
```bash
GET /restconf/data/rt:restconf-test
# Doit retourner HTTP 200
```

---

## 📋 Mapping des Scénarios vers restconf-test.yang

| Scénario | Module YANG | Structures utilisées |
|----------|-------------|---------------------|
| SQ-001 | restconf-test | `/rt:restconf-test/rt:interfaces/rt:interface` (list) |
| SQ-002 | restconf-test | PATCH sur `/rt:restconf-test/rt:interfaces/rt:interface=eth0` |
| SQ-003 | restconf-test | DELETE sur `/rt:restconf-test/rt:interfaces/rt:interface=eth0` |
| SQ-004 | restconf-test | Contrainte `mandatory` sur `/rt:restconf-test/rt:basic-data/rt:device-id` |
| SQ-005 | restconf-test | Datastores `/ds/ietf-datastores:running/rt:restconf-test` |
| SQ-006 | restconf-test | Persistance après redémarrage |
| SQ-007 | restconf-test | Streams `rt:system-startup`, `rt:event-notification` |
| SQ-008 | restconf-test | RPC `rt:configure-device`, `rt:get-system-status` |
| SQ-009 | restconf-test | Accès read-only à `/rt:restconf-test/rt:basic-data/rt:uptime` |
| SQ-010 | restconf-test | Tous les modules ci-dessus |

---

Les scénarios de ce document regroupent plusieurs cas de test afin de
valider une fonctionnalité complète.

Chaque scénario peut être exécuté indépendamment.

---

# SQ-001 Création d'une interface

Objectif

Créer une interface réseau complète.

Cas de test utilisés

TC-3-011
TC-3-018
TC-6-010
TC-7-014

Déroulement

GET running

↓

POST interface

↓

Lecture running

↓

Lecture intended

↓

Lecture operational

↓

Suppression

Résultat attendu

L'interface apparaît dans tous les datastores concernés.

---

# SQ-002 Modification d'une interface

GET interface

↓

PATCH

↓

GET running

↓

GET operational

↓

Validation

---

# SQ-003 Suppression d'une interface

GET

↓

DELETE

↓

GET

↓

404

---

# SQ-004 Validation des contraintes

Créer une interface.

↓

Supprimer une feuille mandatory.

↓

PATCH.

↓

HTTP400 attendu.

---

# SQ-005 Validation NMDA

Création

↓

Running

↓

Intended

↓

Operational

↓

with-origin

↓

Suppression

---

# SQ-006 Redémarrage

Créer configuration

↓

Sauvegarde

↓

Restart serveur

↓

Lecture Startup

↓

Lecture Running

↓

Comparaison

---

# SQ-007 Notification

Créer une configuration.

↓

Observer le Stream.

↓

Notification reçue.

---

# SQ-008 RPC

Créer une configuration.

↓

Exécuter RPC.

↓

Valider la réponse.

---

# SQ-009 Utilisateur ReadOnly

Connexion

↓

GET

↓

PATCH

↓

403 attendu

---

# SQ-010 Qualification complète

Infrastructure

↓

CRUD

↓

RPC

↓

Notifications

↓

NMDA

↓

Sécurité

↓

Performance

↓

Validation finale
