# Stratégie de test

# 1. Objectif

Cette stratégie définit l'organisation des campagnes de qualification du
serveur RESTCONF.

Les tests sont classés par domaine fonctionnel afin de faciliter :

- leur automatisation ;
- leur maintenance ;
- leur exécution partielle ;
- leur traçabilité.

---

# 2. Niveaux de qualification

Le référentiel distingue plusieurs campagnes.

| Campagne | Objectif |
|----------|----------|
| Smoke | Vérification rapide |
| Sanity | Validation des fonctions principales |
| Qualification | Validation complète |
| Régression | Validation après modification |
| Performance | Charge et endurance |
| Sécurité | Contrôles de sécurité |

---

# 3. Approche de test

Chaque fonctionnalité est validée selon quatre niveaux.

## Niveau 1

Validation protocolaire.

Exemple :

GET

```
/restconf/data
```

HTTP 200 attendu.

---

## Niveau 2

Validation fonctionnelle.

Exemple :

Création d'une ressource.

Lecture.

Modification.

Suppression.

---

## Niveau 3

Validation métier.

Les contraintes du modèle YANG sont vérifiées.

Exemple :

Le type `oven-temperature` accepte uniquement des valeurs comprises entre
0 et 250.

Les valeurs suivantes doivent être testées :

| Valeur | Résultat attendu |
|---------|------------------|
| 0 | Acceptée |
| 180 | Acceptée |
| 250 | Acceptée |
| 251 | Rejetée |
| -1 | Rejetée |

---

## Niveau 4

Validation de robustesse.

Tests :

- concurrence ;
- erreurs ;
- charge ;
- sécurité.

---

# 4. Cas d'utilisation de référence : le four Sysrepo

Le modèle `oven.yang` est utilisé tout au long de la qualification.

## Étape 1 : Allumer le four

PATCH

```
/restconf/data/oven:oven
```

Payload :

```json
{
  "oven:oven": {
    "turned-on": true
  }
}
```

---

## Étape 2 : Régler la température

PATCH

```json
{
  "oven:oven": {
    "temperature": 180
  }
}
```

---

## Étape 3 : Vérifier la configuration

GET

```
/restconf/data/oven:oven
```

Résultat attendu :

```json
{
  "oven:oven": {
    "turned-on": true,
    "temperature": 180
  }
}
```

---

## Étape 4 : Vérifier l'état opérationnel

GET

```
/restconf/data/oven:oven-state
```

Le serveur doit retourner les données `config false`, par exemple :

```json
{
  "oven:oven-state": {
    "temperature": 176,
    "food-inside": false
  }
}
```

---

## Étape 5 : Insérer le plat

POST

```
/restconf/operations/oven:insert-food
```

Payload :

```json
{
  "input": {
    "time": "on-oven-ready"
  }
}
```

Le serveur doit accepter le RPC.

---

## Étape 6 : Attendre la notification

Souscrire au flux de notifications.

Lorsque la température réelle atteint 180°C, la notification suivante est
attendue :

```
oven-ready
```

---

## Étape 7 : Retirer le plat

POST

```
/restconf/operations/oven:remove-food
```

Le RPC doit être exécuté avec succès.

---

# 5. Automatisation

Chaque étape précédente correspond à un scénario `pytest` indépendant.

Exemple :

```
test_turn_on_oven()

↓

test_set_temperature()

↓

test_read_configuration()

↓

test_insert_food()

↓

test_wait_notification()

↓

test_remove_food()
```

Ces scénarios seront réutilisés dans les campagnes Smoke, Sanity,
Qualification et Régression.

---

# 6. Critères de succès

Une campagne est considérée comme réussie lorsque :

- toutes les opérations RESTCONF retournent le code HTTP attendu ;
- les données sont conformes au modèle YANG ;
- les contraintes sont respectées ;
- les RPC exécutent l'action attendue ;
- les notifications sont correctement émises ;
- aucune erreur inattendue n'est observée.