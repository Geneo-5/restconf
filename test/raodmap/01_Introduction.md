# 1. Introduction

## 1.1 Objet

Ce document décrit la stratégie de validation fonctionnelle d'un serveur RESTCONF conforme aux spécifications de l'IETF.

L'objectif est de définir l'ensemble des scénarios permettant de vérifier la conformité du serveur vis-à-vis des RFC applicables, de garantir son interopérabilité avec les clients RESTCONF et de détecter les éventuelles régressions lors des évolutions du produit.

Les tests décrits sont indépendants de toute implémentation logicielle. Ils peuvent être exécutés manuellement ou être automatisés dans un framework de test (pytest dans notre cas).

---

## 1.2 Objectifs

La campagne de validation doit permettre de vérifier :

- la conformité au protocole RESTCONF ;
- le respect des modèles YANG publiés par le serveur ;
- la gestion correcte des ressources RESTCONF ;
- la conformité des codes HTTP retournés ;
- la gestion des erreurs ;
- la prise en charge des datastores NMDA ;
- la sécurité des échanges ;
- la robustesse du serveur face aux erreurs d'utilisation ;
- la stabilité sous charge.

---

## 1.3 Références

### RFC 8040

RESTCONF Protocol

Définit :

- les ressources RESTCONF
- les méthodes HTTP
- les paramètres de requête
- les formats XML/JSON
- les codes d'erreur
- les RPC
- les notifications

---

### RFC 8527

RESTCONF Extensions for NMDA

Décrit :

- les nouveaux datastores
- with-origin
- operational datastore
- intended datastore
- running datastore

---

### RFC 6415

Host Metadata

Décrit le mécanisme de découverte du point d'entrée RESTCONF via :

```
/.well-known/host-meta
```

---

## 1.4 Hors périmètre

Cette campagne ne couvre pas :

- les performances réseau
- la validation des modèles YANG eux-mêmes
- les tests unitaires
- l'interface NETCONF
- la validation métier des modèles

---

# 2. Architecture RESTCONF

## 2.1 Vue générale

Un serveur RESTCONF expose un ensemble de ressources accessibles via HTTPS.

Toutes les ressources sont construites à partir des modèles YANG implémentés par le serveur.

Le client ne découvre pas dynamiquement les ressources ; il les construit à partir des modèles YANG publiés.

L'architecture générale est la suivante :

```
HTTPS
   │
   ▼
RESTCONF Root
   │
   ├── data
   │
   ├── operations
   │
   ├── streams
   │
   └── yang-library-version
```

---

## 2.2 Cycle de vie d'une requête

Chaque requête suit les étapes suivantes :

1. établissement de la connexion TLS
2. authentification
3. autorisation
4. validation de l'URI
5. validation des paramètres
6. exécution de l'opération
7. validation du modèle YANG
8. génération de la réponse
9. fermeture de la requête

Chaque étape est susceptible de retourner une erreur HTTP spécifique.

---

## 2.3 Types de ressources

Le protocole distingue plusieurs familles de ressources.

### API Root

```
/restconf
```

Point d'entrée de l'API.

---

### Data Resource

```
/restconf/data
```

Contient l'ensemble des données YANG.

---

### Operation Resource

```
/restconf/operations
```

Expose les RPC et Actions.

---

### Datastore Resource

```
/restconf/ds
```

Ajouté par la RFC8527.

---

### Event Stream

```
/restconf/streams
```

Notifications.

---

### Schema Resource

Permet de récupérer un module YANG.

---

# 3. Stratégie de validation

La validation est organisée selon plusieurs niveaux.

## Niveau 1

Infrastructure

Objectif :

Vérifier que le serveur est accessible.

Exemples :

- HTTPS
- authentification
- découverte RESTCONF

---

## Niveau 2

Conformité protocolaire

Objectif :

Vérifier que chaque méthode HTTP respecte la RFC.

Exemples :

- GET
- POST
- PATCH
- DELETE
- PUT

---

## Niveau 3

Conformité YANG

Objectif :

Vérifier que toutes les ressources respectent les modèles publiés.

---

## Niveau 4

Gestion des erreurs

Objectif :

Vérifier toutes les réponses anormales.

---

## Niveau 5

Sécurité

Objectif :

Tester les contrôles d'accès et la robustesse.

---

## Niveau 6

Performance

Objectif :

Mesurer les performances du serveur.

---

# 4. Classification des cas de test

Chaque cas de test est identifié de manière unique.

Format :

```
TC-<Chapitre>-<Numéro>
```

Exemple :

```
TC-2-014
```

---

Chaque test comporte systématiquement les rubriques suivantes.

## Objectif

Pourquoi ce test existe.

---

## Références

RFC concernées.

Exemple :

```
RFC8040 §4.3
RFC8040 §7
```

---

## Prérequis

Configuration nécessaire.

Exemple :

- serveur démarré
- utilisateur authentifié
- modèle installé

---

## Requête

Méthode HTTP

URI

Headers

Payload éventuel

---

## Vérifications

Liste exhaustive des points à contrôler.

---

## Critères d'acceptation

Conditions nécessaires pour déclarer le test conforme.

---

# 5. Priorisation des tests

Chaque cas de test est classé selon sa criticité.

| Niveau | Description |
|---------|-------------|
| Critique | Fonction indispensable au protocole |
| Haute | Fonction principale |
| Moyenne | Fonction secondaire |
| Faible | Fonction optionnelle |

---

# 6. Ordre recommandé d'exécution

Les campagnes doivent respecter l'ordre suivant :

1. Infrastructure
2. Découverte RESTCONF
3. Authentification
4. Lecture
5. Création
6. Modification
7. Suppression
8. RPC
9. Notifications
10. NMDA
11. Robustesse
12. Sécurité
13. Performance
14. Régression

Cet ordre garantit que les prérequis nécessaires à chaque catégorie de tests sont satisfaits avant l'exécution des scénarios plus avancés.