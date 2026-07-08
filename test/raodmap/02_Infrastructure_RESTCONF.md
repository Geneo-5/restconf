# 02 - Validation de l'infrastructure RESTCONF

# 1. Objectif

Cette phase valide que le serveur expose correctement son interface RESTCONF conformément aux RFC 8040 et RFC 6415.

Aucune opération métier n'est réalisée durant cette phase.

L'objectif est uniquement de vérifier :

- la disponibilité du serveur ;
- la découverte de l'API ;
- la négociation HTTP ;
- les capacités annoncées ;
- les ressources obligatoires.

---

# 2. Prérequis

Avant l'exécution des tests, les conditions suivantes doivent être remplies :

- le serveur RESTCONF est démarré ;
- le serveur est accessible via HTTPS ;
- au moins un utilisateur valide existe ;
- au moins un modèle YANG est installé ;
- le serveur possède un certificat TLS valide.

---

# 3. Découverte RESTCONF

## TC-2-001 — Présence du document Host Metadata

### Objectif

Vérifier que le serveur publie le document Host Metadata.

### Référence

RFC6415

RFC8040 §3.1

### Requête

GET

```
/.well-known/host-meta
```

### Vérifications

- le serveur répond

- HTTP 200

- le Content-Type est compatible avec XRD

- le document est correctement formé

- aucune erreur XML

### Critères d'acceptation

Le document est retourné sans erreur.

---

## TC-2-002 — Présence du lien RESTCONF

### Objectif

Vérifier que le document Host Metadata contient exactement un lien RESTCONF.

### Vérifications

Le document contient :

```
<Link rel="restconf" ...>
```

Le lien est unique.

### Critères d'acceptation

Un seul lien RESTCONF est présent.

---

## TC-2-003 — URI RESTCONF valide

### Objectif

Vérifier que l'URI publiée dans Host Metadata est valide.

### Vérifications

- URI absolue ou relative valide

- commence par "/"

- aucun caractère interdit

### Critères d'acceptation

L'URI est exploitable directement.

---

## TC-2-004 — Accès au Root RESTCONF

### Objectif

Valider que l'URI découverte est accessible.

### Requête

GET

```
/restconf
```

### Vérifications

HTTP 200

Présence des ressources :

- data

- yang-library-version

operations si disponible

---

## TC-2-005 — Ressource inconnue

### Objectif

Vérifier la réponse sur une ressource inexistante.

### Requête

GET

```
/restconf/unknown
```

### Vérifications

HTTP 404

Structure d'erreur RESTCONF

---

# 4. Validation HTTPS

## TC-2-006 — Refus du protocole HTTP

### Objectif

Vérifier que HTTP n'est pas accepté.

### Requête

```
http://server/restconf
```

### Vérifications

Le serveur :

- refuse la connexion

ou

- redirige vers HTTPS

Suivant son implémentation.

---

## TC-2-007 — Certificat TLS

### Objectif

Vérifier la validité du certificat.

### Vérifications

- certificat présenté

- chaîne valide

- certificat non expiré

- CN ou SAN cohérent

---

## TC-2-008 — Version TLS

### Objectif

Vérifier la version minimale supportée.

### Vérifications

TLS 1.2 minimum

TLS 1.3 si disponible

---

# 5. Authentification

## TC-2-009 — Accès authentifié

### Objectif

Vérifier qu'un utilisateur valide accède au serveur.

### Vérifications

HTTP 200

---

## TC-2-010 — Utilisateur inconnu

### Objectif

Vérifier la réponse pour un utilisateur inexistant.

### Vérifications

HTTP 401

---

## TC-2-011 — Mot de passe incorrect

### Vérifications

HTTP 401

---

## TC-2-012 — Authentification absente

### Vérifications

HTTP 401

Présence éventuelle du header WWW-Authenticate.

---

## TC-2-013 — Authentification invalide

Tester :

- Basic malformé

- jeton vide

- certificat invalide

Résultat attendu

401

---

# 6. Validation des Media Types

## TC-2-014 — JSON

### Requête

Accept

```
application/yang-data+json
```

### Vérifications

Content-Type identique.

---

## TC-2-015 — XML

Accept

```
application/yang-data+xml
```

---

## TC-2-016 — Accept invalide

Accept

```
application/pdf
```

### Vérifications

406

ou

415

selon l'implémentation.

---

# 7. Validation de l'API Root

## TC-2-017 — Présence de "data"

### Vérifications

Le root contient :

```
data
```

---

## TC-2-018 — Présence de yang-library-version

Vérifier la présence du leaf.

---

## TC-2-019 — Présence de operations

Si le serveur implémente des RPC.

---

## TC-2-020 — Lecture du root

Comparer la structure retournée avec la RFC.

---

# 8. Validation de la YANG Library

## TC-2-021 — Lecture de la version

GET

```
/restconf/yang-library-version
```

### Vérifications

HTTP 200

Valeur conforme.

---

## TC-2-022 — Lecture des modules

GET

```
/restconf/data/ietf-yang-library:yang-library
```

### Vérifications

Présence :

- modules

- features

- deviations

- datastores

---

## TC-2-023 — Révision des modules

Comparer les révisions avec les modules réellement installés.

---

## TC-2-024 — Module inexistant

Demander un module absent.

Résultat

404

---

# 9. Validation des capacités RESTCONF

## TC-2-025 — Lecture des capacités

Vérifier la liste publiée.

---

## TC-2-026 — Capacité defaults

Si supportée.

Vérifier :

```
with-defaults
```

---

## TC-2-027 — Capacité with-origin

RFC8527

---

## TC-2-028 — Capacité fields

RFC8040

---

## TC-2-029 — Capacité depth

RFC8040

---

## TC-2-030 — Cohérence des capacités

Toutes les capacités annoncées doivent être réellement utilisables.

Une capacité annoncée mais non implémentée constitue une non-conformité.

---

# 10. Validation des Headers HTTP

## TC-2-031 — Content-Type

Toujours cohérent.

---

## TC-2-032 — Cache-Control

Présent si applicable.

---

## TC-2-033 — Date

Header Date valide.

---

## TC-2-034 — ETag

Présent lorsque supporté.

---

## TC-2-035 — Last-Modified

Présent lorsque supporté.

---

# 11. Validation des méthodes HTTP

## TC-2-036 — OPTIONS

Le serveur répond.

---

## TC-2-037 — HEAD

Même comportement que GET sans corps.

---

## TC-2-038 — GET

Réponse conforme.

---

## TC-2-039 — POST sur Root

Doit être refusé.

HTTP 405.

---

## TC-2-040 — DELETE sur Root

Doit être refusé.

HTTP 405.

---

# Critères de sortie

Cette phase est validée lorsque :

- toutes les ressources obligatoires sont accessibles ;
- le serveur respecte les RFC 6415 et 8040 pour la découverte de l'API ;
- les capacités publiées sont cohérentes avec les fonctionnalités implémentées ;
- les négociations HTTP fonctionnent correctement ;
- aucune non-conformité bloquante n'est détectée.