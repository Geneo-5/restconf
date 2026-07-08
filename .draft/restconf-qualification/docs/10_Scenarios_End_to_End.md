# Scénarios End-to-End

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
