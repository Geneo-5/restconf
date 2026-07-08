# 03 - Validation des Ressources RESTCONF

# 1. Objectif

Cette phase valide le comportement des ressources RESTCONF exposées sous :

```

/restconf/data

```

Elle couvre :

- la lecture des données ;
- la création de ressources ;
- leur modification ;
- leur suppression ;
- le respect des contraintes définies par les modèles YANG.

Les tests décrits dans cette section sont indépendants des modèles YANG installés sur le serveur.

---

# 2. Ressources concernées

Les ressources RESTCONF sont construites à partir des nœuds YANG.

Le protocole distingue notamment :

- Container
- Presence Container
- List
- Leaf
- Leaf-list
- Anydata
- Anyxml

Chaque type possède un comportement spécifique qu'il convient de valider.

---

# 3. Validation des requêtes GET

## TC-3-001 — Lecture de la racine des données

### Objectif

Vérifier que la ressource `/data` est accessible.

### Référence

RFC8040 §3.3.1

### Requête

GET

```

/restconf/data

```

### Vérifications

- HTTP 200
- document valide
- Content-Type conforme
- aucune erreur RESTCONF

### Critères d'acceptation

Le serveur retourne l'ensemble des ressources racines accessibles.

---

## TC-3-002 — Lecture d'un container

### Objectif

Vérifier la lecture d'un container.

### Vérifications

Le container est :

- présent
- correctement sérialisé
- conforme au modèle YANG

---

## TC-3-003 — Lecture d'un Presence Container

### Vérifications

Deux cas doivent être distingués :

Container absent :

→ HTTP 404 ou absence dans la représentation.

Container présent :

→ données retournées.

---

## TC-3-004 — Lecture d'une feuille

### Vérifications

- valeur présente
- type conforme
- encodage correct

---

## TC-3-005 — Lecture d'une liste

### Vérifications

- toutes les entrées sont retournées
- aucune duplication
- ordre conforme

---

## TC-3-006 — Lecture d'une entrée de liste

URI

```

/list=key

```

### Vérifications

- bonne entrée
- bonne clé
- données conformes

---

## TC-3-007 — Lecture d'une clé inexistante

Résultat attendu

HTTP 404

---

## TC-3-008 — Lecture d'un leaf-list

### Vérifications

- tableau correctement sérialisé
- ordre conservé si ordered-by user

---

## TC-3-009 — Lecture Anydata

Si supporté.

---

## TC-3-010 — Lecture Anyxml

Si supporté.

---

# 4. Validation POST

## TC-3-011 — Création d'une entrée de liste

### Vérifications

- HTTP 201
- ressource créée
- URI correcte
- Location présent

---

## TC-3-012 — Création d'un container

Si autorisée.

---

## TC-3-013 — Création avec clé absente

Résultat attendu

HTTP 400

---

## TC-3-014 — Création avec clé invalide

Résultat attendu

HTTP 400

---

## TC-3-015 — Création d'un doublon

Créer deux fois la même clé.

Résultat attendu

409 Conflict

---

## TC-3-016 — Création avec données invalides

Tester :

- type incorrect
- longueur invalide
- valeur hors intervalle
- pattern non respecté

Résultat attendu

400 Bad Request

---

## TC-3-017 — Création d'une ressource Read Only

Résultat attendu

405 Method Not Allowed

---

# 5. Validation PUT

## TC-3-018 — Remplacement complet

Remplacer une ressource existante.

### Vérifications

Toutes les anciennes valeurs disparaissent.

---

## TC-3-019 — PUT sur une ressource absente

Vérifier le comportement selon le modèle.

---

## TC-3-020 — PUT avec payload invalide

Résultat attendu

400

---

## TC-3-021 — PUT sur une ressource RO

Résultat attendu

405

---

# 6. Validation PATCH

## TC-3-022 — Modification d'une feuille

Modifier uniquement une leaf.

### Vérifications

Aucune autre donnée ne change.

---

## TC-3-023 — PATCH sur plusieurs feuilles

Toutes les modifications doivent être atomiques.

---

## TC-3-024 — PATCH avec données invalides

Résultat attendu

400

---

## TC-3-025 — PATCH partiel

Le reste de la configuration reste inchangé.

---

## TC-3-026 — PATCH sur ressource inexistante

404

---

# 7. Validation DELETE

## TC-3-027 — Suppression d'une feuille

### Vérifications

La feuille disparaît.

---

## TC-3-028 — Suppression d'un container

Toutes les données filles sont supprimées.

---

## TC-3-029 — Suppression d'une entrée de liste

La clé disparaît.

Les autres entrées restent présentes.

---

## TC-3-030 — DELETE d'une ressource inexistante

404

---

## TC-3-031 — DELETE sur une ressource RO

405

---

# 8. Validation des URI

## TC-3-032 — URI valide

Toutes les URI générées à partir des modèles YANG doivent être acceptées.

---

## TC-3-033 — URI malformée

Tester :

- double slash
- caractère interdit
- espace
- encodage incorrect

Résultat attendu

400

---

## TC-3-034 — Mauvais namespace

404

---

## TC-3-035 — Clé mal encodée

404

---

# 9. Validation des représentations

## TC-3-036 — JSON

Toutes les réponses JSON doivent respecter RFC7951.

Vérifier :

- noms qualifiés
- listes
- namespaces

---

## TC-3-037 — XML

Validation XML.

---

## TC-3-038 — Encodage UTF-8

Tester :

- accents
- caractères Unicode
- emojis si autorisés

---

# 10. Validation des contraintes YANG

## TC-3-039 — Mandatory

Impossible de créer une ressource sans feuille mandatory.

---

## TC-3-040 — Min-elements

Créer moins d'éléments que le minimum.

400 attendu.

---

## TC-3-041 — Max-elements

Dépasser la limite.

400 attendu.

---

## TC-3-042 — Pattern

Tester plusieurs chaînes invalides.

---

## TC-3-043 — Range

Tester :

- borne inférieure
- borne supérieure
- hors intervalle

---

## TC-3-044 — Length

Tester :

- chaîne vide
- longueur maximale
- dépassement

---

## TC-3-045 — Enum

Tester :

- valeur valide
- valeur inconnue

---

## TC-3-046 — Union

Tester chaque type composant l'union.

---

## TC-3-047 — Identityref

Tester :

- identité valide
- identité inconnue

---

## TC-3-048 — Leafref

Créer une référence cassée.

Résultat attendu

400

---

## TC-3-049 — When

Créer une donnée lorsque la condition "when" est fausse.

Résultat attendu

400

---

## TC-3-050 — Must

Créer une configuration violant une contrainte MUST.

Résultat attendu

400

---

# Critères de sortie

Cette phase est validée lorsque :

- toutes les méthodes CRUD fonctionnent conformément à la RFC 8040 ;
- les ressources respectent les modèles YANG ;
- les contraintes YANG sont systématiquement vérifiées ;
- les erreurs sont correctement signalées ;
- aucune incohérence de sérialisation JSON ou XML n'est observée.