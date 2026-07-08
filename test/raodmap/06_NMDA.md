# 06 - Validation des Datastores NMDA (RFC8527)

# 1. Objectif

Cette phase valide les extensions NMDA (Network Management Datastore Architecture)
définies par la RFC8527.

Les objectifs sont les suivants :

- vérifier l'exposition des datastores ;
- vérifier leur contenu ;
- vérifier la cohérence entre les datastores ;
- vérifier les restrictions d'accès ;
- vérifier les métadonnées d'origine (origin).

Cette partie est applicable uniquement si le serveur annonce le support de la RFC8527.

---

# 2. Datastores concernés

Les datastores suivants peuvent être disponibles.

| Datastore | Description | Accès |
|------------|-------------|--------|
| running | Configuration active | Lecture / Écriture |
| intended | Configuration effectivement appliquée | Lecture |
| operational | État opérationnel | Lecture |
| startup | Configuration persistante | Optionnel |
| candidate | Configuration temporaire | Optionnel |

---

# 3. Découverte des datastores

## TC-6-001 — Présence de la ressource `/ds`

### Objectif

Vérifier que le serveur expose les datastores NMDA.

### Requête

GET

```
/restconf/ds
```

### Vérifications

- HTTP 200
- liste des datastores disponible
- document valide

---

## TC-6-002 — Datastore Running

### Vérifications

Le datastore `running` est présent.

---

## TC-6-003 — Datastore Operational

Le datastore `operational` est présent.

---

## TC-6-004 — Datastore Intended

Le datastore `intended` est présent.

---

## TC-6-005 — Datastores optionnels

Vérifier la présence éventuelle de :

- startup
- candidate

Leur absence ne constitue pas une non-conformité.

---

# 4. Validation du datastore Running

## TC-6-006 — Lecture

GET

```
/restconf/ds/ietf-datastores:running
```

### Vérifications

- HTTP 200
- données configurables présentes
- structure valide

---

## TC-6-007 — Écriture

Créer une nouvelle configuration.

### Vérifications

- configuration visible immédiatement
- persistance correcte

---

## TC-6-008 — Suppression

Supprimer une configuration.

### Vérifications

La suppression est effective.

---

# 5. Validation du datastore Intended

## TC-6-009 — Lecture

Le datastore est accessible.

---

## TC-6-010 — Cohérence avec Running

Créer une configuration.

Comparer :

Running

Intended

### Vérifications

Les deux datastores sont cohérents.

---

## TC-6-011 — Configuration invalide

Créer une configuration volontairement invalide.

### Vérifications

Le datastore Intended ne doit jamais contenir
une configuration impossible à appliquer.

---

# 6. Validation du datastore Operational

## TC-6-012 — Lecture

GET

```
/restconf/ds/ietf-datastores:operational
```

### Vérifications

HTTP 200

---

## TC-6-013 — Présence des données opérationnelles

Les feuilles `config false` sont présentes.

---

## TC-6-014 — Absence des données inutiles

Les données non opérationnelles ne doivent pas apparaître.

---

## TC-6-015 — Synchronisation

Modifier Running.

Vérifier que Operational évolue conformément au comportement attendu du système.

---

## TC-6-016 — Lecture seule

Toute tentative de :

- POST
- PUT
- PATCH
- DELETE

doit être refusée.

Résultat attendu :

405

ou

403

selon l'implémentation.

---

# 7. Validation du datastore Startup

Applicable uniquement si annoncé.

## TC-6-017 — Lecture

---

## TC-6-018 — Écriture

---

## TC-6-019 — Persistance après redémarrage

Créer une configuration.

Redémarrer le serveur.

### Vérifications

La configuration est restaurée.

---

# 8. Validation du datastore Candidate

Applicable uniquement si annoncé.

## TC-6-020 — Lecture

---

## TC-6-021 — Modification

---

## TC-6-022 — Isolation

Les modifications Candidate ne doivent pas modifier Running.

---

## TC-6-023 — Validation

Tester le mécanisme de validation avant application.

---

# 9. Validation de `with-origin`

Référence

RFC8527 §5

---

## TC-6-024 — Présence des métadonnées

GET

```
?with-origin=true
```

### Vérifications

Chaque nœud possède une origine.

---

## TC-6-025 — Origine "intended"

Créer une configuration utilisateur.

### Vérifications

Origin = intended

---

## TC-6-026 — Origine "system"

Les données générées automatiquement
portent l'origine "system".

---

## TC-6-027 — Origine "learned"

Si le serveur supporte des informations apprises dynamiquement.

---

## TC-6-028 — Valeur invalide

```
with-origin=invalid
```

HTTP 400 attendu.

---

# 10. Cohérence entre datastores

## TC-6-029 — Running → Operational

Créer une configuration.

Comparer les deux datastores.

---

## TC-6-030 — Suppression

Supprimer une configuration.

Comparer.

---

## TC-6-031 — Modification

Modifier une feuille.

Comparer.

---

## TC-6-032 — Données opérationnelles

Les données `config false`
ne doivent apparaître que dans Operational.

---

# 11. Validation des erreurs

## TC-6-033 — Datastore inexistant

GET

```
/restconf/ds/example
```

HTTP 404

---

## TC-6-034 — URI invalide

HTTP 400

---

## TC-6-035 — Méthode interdite

Tester :

PUT

PATCH

DELETE

sur un datastore en lecture seule.

---

## TC-6-036 — Accès interdit

Utilisateur sans privilèges.

HTTP 403

---

# 12. Performance

## TC-6-037

Lecture simultanée
de Running et Operational.

---

## TC-6-038

Comparaison de gros volumes
de données.

---

## TC-6-039

Accès concurrents.

Plusieurs clients.

---

# Critères de sortie

Cette phase est validée lorsque :

- tous les datastores annoncés sont accessibles ;
- leurs comportements sont conformes à la RFC8527 ;
- les opérations interdites sont correctement rejetées ;
- les métadonnées d'origine sont cohérentes ;
- la cohérence entre Running, Intended et Operational est vérifiée.