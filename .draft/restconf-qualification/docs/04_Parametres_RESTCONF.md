# 04 - Validation des paramètres RESTCONF

# 1. Objectif

Cette section valide le comportement des paramètres de requête définis par la RFC 8040.

Ces paramètres permettent de modifier la représentation des données sans modifier leur contenu.

Les paramètres concernés sont :

- content
- depth
- fields
- filter
- insert
- point
- with-defaults
- with-origin (RFC8527)

Chaque paramètre doit être testé :

- individuellement ;
- en combinaison avec les autres paramètres compatibles ;
- avec des valeurs invalides.

---

# 2. Paramètre `content`

## Description

Le paramètre `content` contrôle le type de données retournées.

Valeurs autorisées :

- config
- nonconfig
- all

Référence :

RFC8040 §4.8.1

---

## TC-4-001 — content=config

### Objectif

Vérifier que seules les données de configuration sont retournées.

### Requête

GET

```
/restconf/data/example:system?content=config
```

### Vérifications

- HTTP 200
- aucune donnée opérationnelle
- toutes les données retournées sont configurables

### Critères d'acceptation

Aucune donnée `config false` ne doit apparaître.

---

## TC-4-002 — content=nonconfig

### Objectif

Vérifier que seules les données opérationnelles sont retournées.

### Vérifications

- aucune donnée configurable
- uniquement les nœuds `config false`

---

## TC-4-003 — content=all

### Vérifications

Toutes les données sont présentes.

---

## TC-4-004 — Valeur invalide

Exemple

```
content=invalid
```

### Vérifications

- HTTP 400
- message d'erreur explicite

---

## TC-4-005 — Paramètre dupliqué

```
content=config&content=all
```

### Vérifications

Le comportement est conforme à la RFC ou rejeté.

---

# 3. Paramètre `depth`

## Description

Permet de limiter la profondeur de l'arbre retourné.

Référence

RFC8040 §4.8.2

---

## TC-4-006 — depth=1

### Vérifications

Seuls les enfants directs sont retournés.

---

## TC-4-007 — depth=2

Vérifier la profondeur exacte.

---

## TC-4-008 — depth=3

Comparer avec le modèle YANG.

---

## TC-4-009 — depth=unbounded

Toutes les données doivent être présentes.

---

## TC-4-010 — depth=0

Valeur interdite.

HTTP 400 attendu.

---

## TC-4-011 — depth négatif

```
depth=-1
```

HTTP 400

---

## TC-4-012 — depth non numérique

```
depth=abc
```

HTTP 400

---

## TC-4-013 — depth très grand

```
depth=1000
```

Le serveur ne doit pas planter.

---

# 4. Paramètre `fields`

## Description

Permet de sélectionner les champs retournés.

Référence

RFC8040 §4.8.3

---

## TC-4-014 — Sélection d'une feuille

```
fields=name
```

### Vérifications

Une seule feuille est présente.

---

## TC-4-015 — Plusieurs feuilles

```
fields=name;description
```

---

## TC-4-016 — Sélection d'un container

---

## TC-4-017 — Sélection d'une liste

---

## TC-4-018 — Sélection imbriquée

```
fields=interfaces/interface(name;enabled)
```

### Vérifications

Structure correcte.

---

## TC-4-019 — Champ inexistant

HTTP 400

---

## TC-4-020 — Syntaxe invalide

Tester :

- parenthèse manquante
- séparateur invalide
- caractère interdit

---

# 5. Paramètre `filter`

## Description

Permet de limiter les données retournées.

Le serveur peut supporter différents mécanismes de filtrage selon son implémentation.

---

## TC-4-021 — Filtre simple

Filtre sur une valeur existante.

---

## TC-4-022 — Filtre sans résultat

La réponse est vide mais valide.

---

## TC-4-023 — Filtre complexe

Combiner plusieurs critères.

---

## TC-4-024 — Filtre invalide

Erreur HTTP 400.

---

## TC-4-025 — Injection XPath

Tester plusieurs expressions malveillantes.

Le serveur ne doit jamais :

- planter
- divulguer d'informations internes
- retourner une erreur système

---

# 6. Paramètre `with-defaults`

Référence

RFC6243

---

## TC-4-026 — trim

Les valeurs par défaut ne sont pas retournées.

---

## TC-4-027 — explicit

Seules les valeurs explicitement configurées apparaissent.

---

## TC-4-028 — report-all

Toutes les valeurs sont présentes.

---

## TC-4-029 — report-all-tagged

Les valeurs par défaut sont identifiables.

---

## TC-4-030 — Valeur invalide

HTTP 400

---

# 7. Paramètre `insert`

Applicable aux listes ordonnées.

Référence

RFC8040 §4.8.5

---

## TC-4-031 — insert=first

Nouvelle entrée insérée en première position.

---

## TC-4-032 — insert=last

Nouvelle entrée insérée en dernière position.

---

## TC-4-033 — insert=before

Entrée correctement positionnée.

---

## TC-4-034 — insert=after

Entrée correctement positionnée.

---

## TC-4-035 — insert sur une liste non ordonnée

Le serveur doit rejeter la requête.

---

## TC-4-036 — insert invalide

HTTP 400

---

# 8. Paramètre `point`

Référence

RFC8040

---

## TC-4-037 — Point valide

Insertion avant une clé existante.

---

## TC-4-038 — Point inexistant

HTTP 409 attendu.

---

## TC-4-039 — Point mal formé

HTTP 400

---

# 9. Paramètre `with-origin`

RFC8527

---

## TC-4-040 — with-origin=true

Chaque donnée doit contenir son origine.

---

## TC-4-041 — Vérification des origines

Comparer :

- running
- intended
- system
- learned

Selon les capacités du serveur.

---

## TC-4-042 — Valeur invalide

HTTP 400

---

# 10. Combinaisons de paramètres

Les paramètres doivent également être validés lorsqu'ils sont utilisés simultanément.

---

## TC-4-043

```
content=config
depth=2
```

---

## TC-4-044

```
fields
depth
```

---

## TC-4-045

```
fields
content
```

---

## TC-4-046

```
fields
depth
content
```

---

## TC-4-047

```
with-defaults
content
```

---

## TC-4-048

```
depth
with-origin
```

---

## TC-4-049

Combinaison de tous les paramètres compatibles.

Le serveur doit produire une réponse cohérente.

---

# 11. Validation des erreurs

## TC-4-050

Paramètre inconnu.

```
?unknown=value
```

---

## TC-4-051

Même paramètre présent plusieurs fois.

---

## TC-4-052

Valeur vide.

```
depth=
```

---

## TC-4-053

Paramètre sans valeur.

```
?depth
```

---

## TC-4-054

Encodage URL invalide.

---

## TC-4-055

Nombre très important de paramètres.

Le serveur doit :

- rester disponible ;
- retourner une erreur contrôlée si nécessaire.

---

# Critères de sortie

Cette phase est validée lorsque :

- tous les paramètres RFC8040 sont correctement interprétés ;
- les combinaisons autorisées produisent des réponses cohérentes ;
- les paramètres invalides sont rejetés avec des erreurs conformes ;
- aucune incohérence de représentation n'est observée ;
- les fonctionnalités optionnelles annoncées par le serveur sont effectivement opérationnelles.