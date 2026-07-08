# 07 - Validation des erreurs et des contraintes YANG

## ⚠️ Prérequis Module YANG

**Tous les tests de cette phase DOIVENT utiliser le module `restconf-test.yang`** (namespace: `urn:restconf:test`, prefix: `rt`).

Ce module contient toutes les contraintes YANG nécessaires pour tester les erreurs :
- `mandatory` leaf (TC-7-014)
- `range` (TC-7-018)
- `pattern` (TC-7-017)
- `length` (TC-7-019)
- `enum` (TC-7-020)
- `must` (TC-7-031)
- `when` (TC-7-032)
- `unique` (TC-7-033)
- `min-elements`/`max-elements` (TC-7-015, TC-7-016)
- Types avancés : `union`, `bits`, `decimal64`, `binary`, `identityref`, `leafref` (TC-7-021-030)

---

## 📋 Compatibilité avec restconf-test.yang

| Cas de test | Applicable | Structure YANG | URI/Opération de test |
|-------------|------------|----------------|----------------------|
| TC-7-001 à TC-7-003 | ✅ | Codes HTTP | Toutes les opérations |
| TC-7-004 | ✅ | HTTP 400 | Requêtes invalides sur `/rt:restconf-test` |
| TC-7-005 | ✅ | HTTP 401 | Accès non authentifié |
| TC-7-006 | ✅ | HTTP 403 | Accès interdit |
| TC-7-007 | ✅ | HTTP 404 | Ressource absente `/rt:restconf-test/rt:nonexistent` |
| TC-7-008 | ✅ | HTTP 405 | Méthode interdite sur `/rt:restconf-test/rt:basic-data/rt:uptime` |
| TC-7-009 | ✅ | HTTP 406 | Accept invalide |
| TC-7-010 | ✅ | HTTP 409 | Conflit de clé dans `/rt:restconf-test/rt:interfaces/rt:interface` |
| TC-7-011 | ⚠️ | HTTP 412 | ETag (si supporté) |
| TC-7-012 | ✅ | HTTP 415 | Content-Type non supporté |
| TC-7-013 | ✅ | HTTP 500 | Erreur interne contrôlée |
| TC-7-014 | ✅ | Mandatory | POST `/rt:restconf-test/rt:basic-data` sans `device-id` |
| TC-7-015 | ✅ | Min-elements | POST `/rt:restconf-test/rt:interfaces` avec 0 VLAN |
| TC-7-016 | ✅ | Max-elements | POST `/rt:restconf-test/rt:interfaces/rt:vlan` (4095+ entrées) |
| TC-7-017 | ✅ | Pattern | POST `/rt:restconf-test/rt:basic-data` avec `device-id` invalide |
| TC-7-018 | ✅ | Range | POST `/rt:restconf-test/rt:constraints/rt:range-test` avec valeur hors 0-100 |
| TC-7-019 | ✅ | Length | POST `/rt:restconf-test/rt:constraints/rt:password` avec chaîne trop courte/longue |
| TC-7-020 | ✅ | Enum | POST `/rt:restconf-test/rt:constraints/rt:operation-mode` avec valeur invalide |
| TC-7-021 | ✅ | Bits | POST `/rt:restconf-test/rt:advanced-types/rt:feature-flags` avec bit inconnu |
| TC-7-022 | ✅ | Decimal64 | POST `/rt:restconf-test/rt:advanced-types/rt:precision-value` hors plage |
| TC-7-023 | ✅ | Boolean | POST avec `true`/`false`/`TRUE`/`0`/`1`/invalide |
| TC-7-024 | ✅ | Uint | POST avec valeurs aux bornes |
| TC-7-025 | ⚠️ | Int | Types int8/int16/int32/int64 |
| TC-7-026 | ✅ | Identityref | POST `/rt:restconf-test/rt:advanced-types/rt:sensor-type-ref` avec identité inconnue |
| TC-7-027 | ✅ | Leafref | POST avec référence cassée |
| TC-7-028 | ✅ | Empty | - |
| TC-7-029 | ✅ | Union | POST `/rt:restconf-test/rt:advanced-types/rt:mixed-value` avec type invalide |
| TC-7-030 | ✅ | Binary | POST `/rt:restconf-test/rt:advanced-types/rt:certificate` avec base64 invalide |
| TC-7-031 | ✅ | Must | POST `/rt:restconf-test/rt:constraints/rt:must-test` avec `start-time >= end-time` |
| TC-7-032 | ✅ | When | POST `/rt:restconf-test/rt:interfaces/rt:advanced-features/rt:advanced-interface` quand `enable-advanced=false` |
| TC-7-033 | ✅ | Unique | POST `/rt:restconf-test/rt:constraints/rt:unique-test/rt:items` avec duplication `name+priority` |
| TC-7-034/035 | ✅ | Ordered-by | Liste `ordered-queue` |
| TC-7-036 à TC-7-042 | ✅ | Listes | Divers tests sur `/rt:restconf-test/rt:interfaces/rt:interface` |
| TC-7-043 à TC-7-045 | ✅ | Anydata/Anyxml | POST avec données invalides |
| TC-7-046 à TC-7-051 | ✅ | URI | URI malformées/longues/caractères interdits |
| TC-7-052 à TC-7-057 | ✅ | JSON | JSON valide/invalide/champs supplémentaires |
| TC-7-058 à TC-7-061 | ✅ | XML | XML valide/invalide/namespace incorrect |
| TC-7-062 à TC-7-067 | ✅ | Messages d'erreur | Structure conforme RFC8040 |

---

# 1. Objectif

Cette phase vérifie que le serveur RESTCONF détecte correctement toutes les
erreurs pouvant être rencontrées lors de l'utilisation de l'API.

Les objectifs sont :

- vérifier les codes HTTP ;
- vérifier les structures d'erreur RESTCONF ;
- vérifier les contraintes YANG ;
- vérifier la cohérence des messages retournés ;
- vérifier qu'aucune donnée invalide ne peut être enregistrée.

Les tests décrits ici doivent être exécutés sur l'ensemble des modèles
YANG supportés par le serveur.

**Module à utiliser** : `restconf-test.yang` contient toutes les contraintes et types nécessaires pour tester les erreurs.

Toutes les erreurs doivent respecter la structure définie par la RFC8040.

Une réponse d'erreur doit contenir :

- errors
- error-type
- error-tag
- error-message

Si disponible :

- error-app-tag
- error-path
- error-info

---

# 3. Validation des codes HTTP

## TC-7-001 — HTTP 200

Toutes les opérations réussies retournent HTTP 200.

---

## TC-7-002 — HTTP 201

Une création réussie retourne :

HTTP 201

Le header Location doit être présent.

---

## TC-7-003 — HTTP 204

Une suppression réussie retourne :

204 No Content

Le corps de la réponse est vide.

---

## TC-7-004 — HTTP 400

Tester :

- URI invalide
- JSON invalide
- XML invalide
- type incorrect
- paramètre invalide

---

## TC-7-005 — HTTP 401

Tester :

- absence d'authentification
- mot de passe invalide
- certificat invalide

---

## TC-7-006 — HTTP 403

Tester :

- accès interdit
- ressource protégée
- datastore en lecture seule

---

## TC-7-007 — HTTP 404

Tester :

- ressource absente
- clé inexistante
- module inconnu

---

## TC-7-008 — HTTP 405

Tester :

POST

PUT

PATCH

DELETE

sur une ressource ne supportant pas ces méthodes.

---

## TC-7-009 — HTTP 406

Header Accept non supporté.

---

## TC-7-010 — HTTP 409

Tester :

- clé déjà existante
- conflit de configuration
- violation d'unicité

---

## TC-7-011 — HTTP 412

Applicable si ETag est supporté.

Tester If-Match.

---

## TC-7-012 — HTTP 415

Content-Type non supporté.

---

## TC-7-013 — HTTP 500

Déclencher une erreur interne contrôlée.

Le serveur ne doit jamais divulguer
des informations internes.

---

# 4. Validation des contraintes YANG

Toutes les contraintes doivent être vérifiées
avant toute modification du datastore.

---

## TC-7-014 — Mandatory

Créer une ressource sans feuille mandatory.

Résultat attendu

400

---

## TC-7-015 — Min-elements

Créer une liste contenant moins d'éléments
que le minimum.

---

## TC-7-016 — Max-elements

Créer davantage d'éléments
que la limite autorisée.

---

## TC-7-017 — Pattern

Tester :

- chaîne valide
- chaîne invalide
- caractères spéciaux
- UTF-8

---

## TC-7-018 — Range

Tester :

borne inférieure

borne supérieure

hors limite

---

## TC-7-019 — Length

Tester :

chaîne vide

taille minimale

taille maximale

dépassement

---

## TC-7-020 — Enum

Tester :

valeur valide

valeur inconnue

casse différente

---

## TC-7-021 — Bits

Tester :

bit inconnu

bit valide

plusieurs bits

---

## TC-7-022 — Decimal64

Tester :

précision

arrondi

hors limite

---

## TC-7-023 — Boolean

Tester :

true

false

TRUE

0

1

valeurs invalides

---

## TC-7-024 — Uint

Tester toutes les bornes.

---

## TC-7-025 — Int

Tester :

INT8

INT16

INT32

INT64

---

## TC-7-026 — Identityref

Tester :

identité valide

identité inconnue

namespace incorrect

---

## TC-7-027 — Leafref

Créer :

une référence valide

une référence cassée

une référence supprimée

---

## TC-7-028 — Empty

Tester la présence
et l'absence.

---

## TC-7-029 — Union

Tester chacune
des branches possibles.

---

## TC-7-030 — Binary

Tester :

base64 valide

base64 invalide

taille maximale

---

# 5. Validation des contraintes logiques

## TC-7-031 — Constraint MUST

Créer une configuration
violant une expression MUST.

Le serveur doit refuser.

---

## TC-7-032 — Constraint WHEN

Créer une donnée
dont la condition WHEN
n'est pas satisfaite.

---

## TC-7-033 — Unique

Créer deux objets
violant UNIQUE.

---

## TC-7-034 — Ordered-by User

Vérifier
l'ordre exact.

---

## TC-7-035 — Ordered-by System

Le serveur choisit l'ordre.

---

# 6. Validation des listes

## TC-7-036

Liste vide.

---

## TC-7-037

Liste contenant un élément.

---

## TC-7-038

Liste contenant plusieurs centaines d'éléments.

---

## TC-7-039

Liste contenant plusieurs milliers d'éléments.

---

## TC-7-040

Clé dupliquée.

---

## TC-7-041

Clé absente.

---

## TC-7-042

Clé mal encodée.

---

# 7. Validation Anydata / Anyxml

## TC-7-043

Document valide.

---

## TC-7-044

Document invalide.

---

## TC-7-045

Document volumineux.

---

# 8. Validation des URI

## TC-7-046

URI très longue.

---

## TC-7-047

URI contenant des caractères interdits.

---

## TC-7-048

URI mal encodée.

---

## TC-7-049

Namespace invalide.

---

## TC-7-050

Double slash.

---

## TC-7-051

Slash final.

---

# 9. Validation JSON

## TC-7-052

JSON valide.

---

## TC-7-053

JSON mal formé.

---

## TC-7-054

Champ supplémentaire.

---

## TC-7-055

Champ obligatoire absent.

---

## TC-7-056

Tableau invalide.

---

## TC-7-057

Type JSON incorrect.

---

# 10. Validation XML

## TC-7-058

XML valide.

---

## TC-7-059

XML mal formé.

---

## TC-7-060

Namespace XML incorrect.

---

## TC-7-061

Balise inconnue.

---

# 11. Validation des messages d'erreur

## TC-7-062

Présence de error-tag.

---

## TC-7-063

Présence de error-type.

---

## TC-7-064

Présence de error-path.

---

## TC-7-065

Présence de error-info.

---

## TC-7-066

Message lisible.

---

## TC-7-067

Aucune fuite d'information.

Les erreurs ne doivent jamais contenir :

- chemin disque
- stack trace
- nom de classe
- SQL
- adresse mémoire

---

# Critères de sortie

Cette phase est validée lorsque :

- toutes les erreurs RESTCONF sont conformes à la RFC8040 ;
- toutes les contraintes YANG sont appliquées ;
- aucune donnée invalide ne peut être enregistrée ;
- les messages d'erreur sont cohérents, complets et ne divulguent aucune information sensible.