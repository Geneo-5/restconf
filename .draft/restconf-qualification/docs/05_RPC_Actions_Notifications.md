# 05 - Validation des RPC, Actions et Notifications

# 1. Objectif

Cette section valide l'exécution des opérations RESTCONF qui ne manipulent pas directement les ressources de configuration.

Elle couvre :

- les RPC YANG ;
- les Actions YANG ;
- les flux de notifications ;
- la gestion des erreurs ;
- le format des réponses.

Les fonctionnalités décrites sont définies principalement dans :

- RFC 8040
- RFC 7950 (YANG 1.1)

---

# 2. Ressources concernées

Les ressources suivantes sont concernées :

```

/restconf/operations
/restconf/streams

```

Les opérations sont exposées sous la ressource **operations** tandis que les notifications sont accessibles via **streams**.

---

# 3. Validation de la découverte des RPC

## TC-5-001 — Présence de la ressource `/operations`

### Objectif

Vérifier que le serveur expose les RPC disponibles.

### Référence

RFC8040 §3.3

### Requête

GET

```

/restconf/operations

```

### Vérifications

- HTTP 200
- ressource présente
- document valide
- sérialisation JSON/XML conforme

### Critères d'acceptation

La liste des RPC est accessible.

---

## TC-5-002 — RPC déclaré dans un module YANG

### Objectif

Vérifier que chaque RPC déclaré dans un module YANG est exposé par RESTCONF.

### Vérifications

Pour chaque module :

- RPC présent
- namespace correct
- URI correcte

---

## TC-5-003 — RPC inexistant

### Requête

POST

```

/restconf/operations/example:unknown-rpc

```

### Vérifications

- HTTP 404
- erreur RESTCONF

---

# 4. Validation des RPC

## TC-5-004 — Exécution d'un RPC sans paramètre

### Objectif

Valider l'exécution d'un RPC ne nécessitant aucun argument.

### Vérifications

- HTTP 200
- résultat conforme
- absence d'erreur

---

## TC-5-005 — RPC avec paramètres

### Vérifications

- tous les paramètres sont correctement interprétés
- les types sont respectés
- la réponse est conforme au modèle YANG

---

## TC-5-006 — Paramètre obligatoire absent

### Vérifications

- HTTP 400
- erreur explicite
- indication du paramètre manquant

---

## TC-5-007 — Paramètre de type invalide

Exemple :

- chaîne au lieu d'un entier
- booléen invalide
- enum inconnue

Résultat attendu :

HTTP 400

---

## TC-5-008 — Paramètre supplémentaire

Envoyer un paramètre non défini dans le modèle YANG.

### Vérifications

Le serveur :

- rejette la requête

ou

- ignore le paramètre

selon son implémentation et la RFC.

---

## TC-5-009 — Valeurs limites

Tester :

- entier minimal
- entier maximal
- chaîne vide
- chaîne maximale

---

## TC-5-010 — RPC retournant des données

### Vérifications

Le résultat :

- respecte le modèle YANG
- contient uniquement les feuilles attendues
- respecte les types

---

## TC-5-011 — RPC sans résultat

Le corps de la réponse doit être conforme à la RFC.

---

# 5. Validation des erreurs RPC

## TC-5-012 — Erreur métier

Déclencher volontairement une erreur fonctionnelle.

### Vérifications

- HTTP approprié
- error-tag
- error-message
- error-type

---

## TC-5-013 — Erreur interne

Le serveur ne doit jamais :

- divulguer de stack trace
- divulguer de chemin interne
- divulguer de détails d'implémentation

---

## TC-5-014 — Timeout

Déclencher un RPC long.

### Vérifications

Le comportement est maîtrisé.

---

# 6. Validation des Actions YANG

Les Actions sont définies dans YANG 1.1.

Contrairement aux RPC, elles sont attachées à un nœud de données.

---

## TC-5-015 — Découverte d'une Action

Vérifier qu'une Action déclarée est accessible.

---

## TC-5-016 — Exécution d'une Action

### Vérifications

- HTTP 200
- action exécutée
- résultat conforme

---

## TC-5-017 — Action sur une ressource inexistante

Résultat attendu

404

---

## TC-5-018 — Paramètres invalides

HTTP 400

---

## TC-5-019 — Action interdite

Tester une Action sans les droits nécessaires.

Résultat attendu

403

---

# 7. Validation des Notifications

Les notifications sont accessibles via :

```

/restconf/streams

```

---

## TC-5-020 — Découverte des streams

### Vérifications

Le serveur retourne :

- la liste des streams disponibles
- leurs caractéristiques

---

## TC-5-021 — Présence du stream par défaut

Si applicable.

---

## TC-5-022 — Ouverture d'un stream

### Vérifications

Connexion établie.

---

## TC-5-023 — Réception d'une notification

Déclencher un événement.

### Vérifications

- notification reçue
- format conforme
- namespace correct

---

## TC-5-024 — Plusieurs notifications

Le serveur conserve l'ordre d'émission.

---

## TC-5-025 — Notification vide

Le serveur ne génère pas de notification invalide.

---

## TC-5-026 — Notification avec données

Validation complète du contenu.

---

## TC-5-027 — Déconnexion du client

Le serveur libère correctement les ressources.

---

## TC-5-028 — Reconnexion

Le client peut se reconnecter.

---

# 8. Validation des formats

## TC-5-029 — JSON

Toutes les notifications JSON sont conformes à RFC7951.

---

## TC-5-030 — XML

Validation XML.

---

# 9. Validation de la sécurité

## TC-5-031 — Accès non authentifié

Les RPC doivent être protégés.

Résultat attendu

401

---

## TC-5-032 — Accès non autorisé

Résultat attendu

403

---

## TC-5-033 — Action interdite

Le contrôle d'accès est appliqué.

---

## TC-5-034 — Notifications protégées

Les streams non autorisés ne doivent pas être accessibles.

---

# 10. Robustesse

## TC-5-035 — Payload volumineux

Tester des paramètres très volumineux.

---

## TC-5-036 — Payload vide

Comportement conforme.

---

## TC-5-037 — JSON invalide

HTTP 400

---

## TC-5-038 — XML invalide

HTTP 400

---

## TC-5-039 — Exécutions simultanées

Plusieurs RPC en parallèle.

### Vérifications

- aucune corruption
- aucune fuite mémoire
- réponses indépendantes

---

## TC-5-040 — Interruption réseau

Interrompre la connexion durant l'exécution.

Le serveur doit nettoyer correctement le contexte.

---

# Critères de sortie

Cette phase est validée lorsque :

- tous les RPC exposés sont exécutables conformément au modèle YANG ;
- les Actions respectent les contraintes du modèle ;
- les Notifications sont correctement publiées et sérialisées ;
- les erreurs sont conformes à la RFC 8040 ;
- les contrôles d'accès sont correctement appliqués ;
- aucune fuite d'information ou comportement anormal n'est observé.