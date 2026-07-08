# 08 - Validation de la sécurité

# 1. Objectif

Cette phase vérifie que le serveur RESTCONF protège correctement les
données de configuration et les ressources exposées.

Les objectifs sont les suivants :

- vérifier les mécanismes d'authentification ;
- vérifier les mécanismes d'autorisation ;
- vérifier la confidentialité des échanges ;
- vérifier la résistance aux attaques courantes ;
- vérifier l'absence de fuite d'informations.

Les tests décrits dans cette section sont indépendants de
l'implémentation du serveur.

---

# 2. Authentification

## TC-8-001 — Authentification valide

### Objectif

Vérifier qu'un utilisateur autorisé peut accéder au serveur.

### Vérifications

- HTTP 200
- accès autorisé
- aucune erreur

---

## TC-8-002 — Utilisateur inconnu

### Vérifications

HTTP 401

---

## TC-8-003 — Mot de passe invalide

HTTP 401

---

## TC-8-004 — Mot de passe vide

HTTP 401

---

## TC-8-005 — Header Authorization absent

HTTP 401

---

## TC-8-006 — Header Authorization malformé

Exemple :

Authorization: Basic XXXXX

Le serveur refuse la requête.

---

## TC-8-007 — Tentatives répétées

Effectuer plusieurs authentifications invalides successives.

### Vérifications

Selon la politique de sécurité :

- limitation de débit ;
- verrouillage temporaire ;
- journalisation.

---

# 3. Autorisation

## TC-8-008 — Lecture autorisée

Un utilisateur disposant des droits de lecture peut consulter les données.

---

## TC-8-009 — Écriture interdite

Un utilisateur en lecture seule tente un PATCH.

Résultat attendu :

403 Forbidden

---

## TC-8-010 — Suppression interdite

DELETE refusé.

---

## TC-8-011 — RPC interdit

Exécuter un RPC nécessitant des privilèges élevés.

HTTP 403

---

## TC-8-012 — Action interdite

Même principe.

---

# 4. Contrôle d'accès (NACM)

Applicable uniquement si le serveur implémente
RFC8341 (NACM).

## TC-8-013

Lecture refusée.

---

## TC-8-014

Écriture refusée.

---

## TC-8-015

Masquage d'un sous-arbre YANG.

Le nœud ne doit pas apparaître.

---

## TC-8-016

Masquage d'une feuille.

---

## TC-8-017

Masquage d'une liste.

---

# 5. Validation TLS

## TC-8-018

TLS 1.2 minimum.

---

## TC-8-019

TLS 1.3 si disponible.

---

## TC-8-020

Rejet de SSLv2.

---

## TC-8-021

Rejet de SSLv3.

---

## TC-8-022

Rejet de TLS1.0.

---

## TC-8-023

Rejet de TLS1.1.

---

## TC-8-024

Validation du certificat serveur.

---

## TC-8-025

Validation de la chaîne de certification.

---

## TC-8-026

Expiration du certificat.

---

## TC-8-027

Nom DNS invalide.

---

# 6. Validation des Headers HTTP

## TC-8-028

Absence de fuite de version.

Le header Server ne doit pas divulguer
la version exacte du logiciel.

---

## TC-8-029

Validation du header Date.

---

## TC-8-030

Validation du Cache-Control.

---

## TC-8-031

Validation du WWW-Authenticate.

---

# 7. Validation des injections

## TC-8-032 — JSON Injection

Envoyer plusieurs structures JSON malveillantes.

Le serveur ne doit jamais :

- planter ;
- accepter la requête ;
- modifier le datastore.

---

## TC-8-033 — XML Injection

Même principe.

---

## TC-8-034 — XPath Injection

Applicable si le serveur supporte des filtres XPath.

---

## TC-8-035 — Injection URI

Tester :

- ../
- //
- %2e
- %2f

---

## TC-8-036 — Caractères spéciaux

Tester :

<

>

"

'

%

NULL

UTF-8

---

# 8. Validation des attaques volumétriques

## TC-8-037

Payload JSON de grande taille.

---

## TC-8-038

Payload XML de grande taille.

---

## TC-8-039

Très grand nombre de feuilles.

---

## TC-8-040

Très grande liste.

---

## TC-8-041

Très grand nombre de requêtes simultanées.

---

# 9. Validation des limites

## TC-8-042

URI très longue.

---

## TC-8-043

Header HTTP très long.

---

## TC-8-044

Grand nombre de headers.

---

## TC-8-045

Query string très longue.

---

## TC-8-046

Nom de feuille très long.

---

## TC-8-047

Valeur très longue.

---

# 10. Validation des erreurs

## TC-8-048

Les erreurs ne doivent jamais contenir :

- stack trace

- chemin disque

- exception Java/C++

- adresse mémoire

---

## TC-8-049

Les erreurs sont cohérentes.

---

## TC-8-050

Les erreurs sont journalisées.

---

# 11. Validation des journaux

## TC-8-051

Connexion réussie.

Présence d'un log.

---

## TC-8-052

Connexion refusée.

Présence d'un log.

---

## TC-8-053

Suppression.

Présence d'un log.

---

## TC-8-054

Erreur serveur.

Présence d'un log.

---

# 12. Validation des sessions

## TC-8-055

Ouverture d'une session.

---

## TC-8-056

Fermeture normale.

---

## TC-8-057

Déconnexion brutale.

---

## TC-8-058

Plusieurs connexions simultanées.

---

## TC-8-059

Réutilisation d'une session expirée.

Le serveur refuse l'accès.

---

# Critères de sortie

Cette phase est validée lorsque :

- seuls les utilisateurs autorisés accèdent aux ressources ;
- les mécanismes TLS sont conformes à la politique de sécurité ;
- les contrôles d'accès sont correctement appliqués ;
- les attaques classiques sont rejetées ;
- aucune fuite d'information sensible n'est observée ;
- les erreurs sont correctement journalisées.