# 09 - Validation des performances, de la robustesse et de la concurrence

## ⚠️ Prérequis Module YANG

**Les tests de cette phase peuvent utiliser `restconf-test.yang`** (namespace: `urn:restconf:test`, prefix: `rt`) comme base pour les tests de charge.

Le module contient suffisamment de structures (listes, containers, leaf-lists) pour générer des volumes importants de données utiles pour les tests de performance.

---

# 1. Objectif

Cette phase a pour objectif de qualifier le comportement du serveur RESTCONF
lorsqu'il est soumis à une utilisation intensive ou à des situations
exceptionnelles.

Les objectifs sont les suivants :

- vérifier la stabilité du serveur ;
- vérifier l'absence de fuite mémoire ;
- mesurer les performances ;
- vérifier le comportement en concurrence ;
- vérifier la récupération après incident.

---

# 2. Tests de montée en charge

## TC-9-001 — Lecture unique

Effectuer une lecture simple.

### Vérifications

- temps de réponse conforme ;
- aucune erreur.

---

## TC-9-002 — 100 lectures simultanées

Exécuter 100 GET simultanément.

### Vérifications

- HTTP 200 pour toutes les requêtes ;
- aucune corruption ;
- temps de réponse stable.

---

## TC-9-003 — 1000 lectures simultanées

Même principe.

Observer :

- CPU
- mémoire
- temps moyen
- temps maximum

---

## TC-9-004 — Lecture continue

Exécuter des GET pendant une heure.

### Vérifications

- stabilité ;
- absence de fuite mémoire.

---

# 3. Tests d'écriture

## TC-9-005

100 créations simultanées.

---

## TC-9-006

100 modifications simultanées.

---

## TC-9-007

100 suppressions simultanées.

---

## TC-9-008

Mélange :

GET

POST

PATCH

DELETE

---

## TC-9-009

Création massive.

Plusieurs milliers de ressources.

---

# 4. Concurrence

## TC-9-010

Deux clients modifient
la même feuille.

### Vérifications

Le comportement est cohérent.

---

## TC-9-011

Deux clients suppriment
la même ressource.

---

## TC-9-012

Lecture pendant une modification.

La réponse ne doit jamais être incohérente.

---

## TC-9-013

Modification pendant une suppression.

---

## TC-9-014

Plusieurs PATCH simultanés.

---

## TC-9-015

Modification de plusieurs listes.

---

# 5. Validation des ETag

Applicable si le serveur les supporte.

## TC-9-016

Lecture d'un ETag.

---

## TC-9-017

Modification avec If-Match valide.

---

## TC-9-018

Modification avec If-Match invalide.

HTTP 412 attendu.

---

## TC-9-019

Modification concurrente.

Vérifier le mécanisme de protection.

---

# 6. Robustesse

## TC-9-020

Redémarrage du serveur.

La configuration est conservée.

---

## TC-9-021

Arrêt brutal.

---

## TC-9-022

Interruption réseau.

---

## TC-9-023

Perte de connexion client.

---

## TC-9-024

Timeout.

---

## TC-9-025

Connexion interrompue
pendant un PATCH.

Le datastore reste cohérent.

---

# 7. Utilisation mémoire

## TC-9-026

Lecture répétée.

Observer :

- mémoire ;
- descripteurs ;
- threads.

---

## TC-9-027

Création répétée.

---

## TC-9-028

Suppression répétée.

---

## TC-9-029

Ouverture/Fermeture répétée
de connexions.

---

# 8. Utilisation CPU

## TC-9-030

Lecture massive.

---

## TC-9-031

PATCH massif.

---

## TC-9-032

RPC intensifs.

---

## TC-9-033

Notifications intensives.

---

# 9. Endurance

## TC-9-034

24 heures de GET continus.

---

## TC-9-035

24 heures de PATCH.

---

## TC-9-036

24 heures de notifications.

---

## TC-9-037

24 heures de connexions.

---

# 10. Validation de la récupération

## TC-9-038

Redémarrage après panne.

---

## TC-9-039

Reconstruction des sessions.

---

## TC-9-040

Rechargement des modules YANG.

---

## TC-9-041

Reprise des notifications.

---

# 11. Régression

Cette campagne est exécutée
à chaque nouvelle version du serveur.

Les catégories suivantes sont exécutées :

- Infrastructure
- CRUD
- RPC
- Actions
- Notifications
- NMDA
- Sécurité

---

# Critères de sortie

La phase est validée lorsque :

- aucune fuite mémoire n'est observée ;
- le serveur reste disponible pendant toute la durée des essais ;
- les performances restent dans les limites attendues ;
- les accès concurrents ne provoquent aucune corruption des données ;
- les mécanismes de reprise fonctionnent correctement.