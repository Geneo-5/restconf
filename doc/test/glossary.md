# Glossaire

## ⚠️ Module YANG de référence

Les définitions de ce glossaire s'appliquent au module `restconf-test.yang` (namespace: `urn:restconf:test`, prefix: `rt`), qui est le module principal utilisé pour les tests de qualification.

Le module `oven.yang` est utilisé comme exemple simple dans certaines illustrations, mais **les tests de conformité doivent utiliser `restconf-test.yang`**.

---

## RESTCONF

Protocole REST défini par la RFC8040 permettant la gestion de données
modélisées en YANG.

---

## YANG

Langage de modélisation utilisé pour décrire :

- la configuration
- les données opérationnelles
- les RPC
- les Notifications

**Module de référence pour les tests** : `restconf-test.yang` (urn:restconf:test)

---

## Datastore

Base de données logique contenant une représentation de la configuration.

Les principaux datastores (RFC8527 NMDA) sont :

- **running** : Configuration active
- **intended** : Configuration effectivement appliquée
- **operational** : État opérationnel (données `config false`)
- **startup** : Configuration persistante (optionnel)
- **candidate** : Configuration temporaire (optionnel)

**Accès via restconf-test.yang** : `/restconf/ds/ietf-datastores:<datastore>/rt:restconf-test`

---

## Container

Nœud YANG pouvant contenir d'autres nœuds.

**Exemple dans restconf-test.yang** : `rt:system`, `rt:basic-data`, `rt:interfaces`

---

## Presence Container

Container dont la présence possède une signification fonctionnelle.

**Exemple dans restconf-test.yang** : `rt:system/rt:config` (presence "System configuration is present")

---

## Leaf

Valeur unique.

**Exemples dans restconf-test.yang** :
- `rt:basic-data/rt:device-id` (string, mandatory)
- `rt:basic-data/rt:timeout` (uint16, default=30)
- `rt:basic-data/rt:enabled` (boolean)

---

## Leaf-list

Liste de valeurs simples (sans clé).

**Exemple dans restconf-test.yang** : `rt:access-control/rt:allowed-ips` (leaf-list de inet:ipv4-address)

---

## List

Collection d'objets indexés par une ou plusieurs clés.

**Exemples dans restconf-test.yang** :
- `rt:interfaces/rt:interface` (key: name)
- `rt:interfaces/rt:vlan` (key: vlan-id, min-elements=1, max-elements=4094)
- `rt:interfaces/rt:ordered-queue` (key: position, ordered-by user)

---

## RPC (Remote Procedure Call)

Opération exécutée sur le serveur.

**RPCs dans restconf-test.yang** :
- `rt:get-system-status` (sans paramètres)
- `rt:configure-device` (avec paramètres d'entrée/sortie)
- `rt:create-resource` (paramètre obligatoire)
- `rt:set-operation-mode` (paramètre enumeration)
- `rt:process-data` (validation de types)
- `rt:trigger-event` (sans sortie)

**URI** : `/restconf/operations/rt:<rpc-name>`

---

## Action

Opération associée à une ressource particulière (YANG 1.1).

**Actions dans restconf-test.yang** :
- `rt:reset` (sur `/rt:restconf-test/rt:device-management`)
- `rt:test-connection` (avec paramètres)
- `rt:reboot` (sur `/rt:restconf-test/rt:device-management/rt:managed-device`)

**URI** : `/restconf/data/rt:restconf-test/rt:device-management/rt:<action-name>`

---

## Notification

Événement envoyé par le serveur.

**Notifications dans restconf-test.yang** :
- `rt:system-startup` (notification simple)
- `rt:threshold-exceeded` (avec données structurées)
- `rt:event-notification` (pour tests de stream)
- `rt:system-alert` (notification d'erreur)

**URI** : `/restconf/streams/rt:<notification-name>`

---

## Capability

Fonctionnalité annoncée par le serveur.

**Exemple** : Le serveur annonce les capacités RESTCONF (depth, fields, with-defaults, with-origin) via le module `ietf-restconf-monitoring`.

---

## NMDA (Network Management Datastore Architecture)

Architecture introduite par la RFC8527.

Définit les datastores : running, intended, operational, startup, candidate.

**Test avec restconf-test.yang** : Accès via `/restconf/ds/ietf-datastores:<name>/rt:restconf-test`

---

## NACM (Network Access Control Model)

Network Access Control Model - RFC8341.

Modèle pour le contrôle d'accès aux ressources RESTCONF.

**Application** : Les tests de sécurité (phase 8) vérifient que NACM est correctement appliqué aux ressources `rt:restconf-test`.

---

## Typedef

Définition d'un type réutilisable.

**Typedefs dans restconf-test.yang** :
- `rt:percentage` (uint8 0..100)
- `rt:device-name` (string avec pattern)
- `rt:serial-number` (string length 8..20)
- `rt:temperature-celsius` (int16 -273..1000)

---

## Identity

Type énuméré extensible.

**Identities dans restconf-test.yang** :
- `rt:sensor-type` (base)
- `rt:temperature-sensor`
- `rt:humidity-sensor`
- `rt:pressure-sensor`

Utilisées pour : `rt:advanced-types/rt:sensor-type-ref` (identityref)

---

## Union

Type qui peut être l'un de plusieurs types.

**Exemple dans restconf-test.yang** : `rt:advanced-types/rt:mixed-value` (union de uint8, string, inet:ipv4-address)

---

## Bits

Ensemble de flags binaires.

**Exemple dans restconf-test.yang** : `rt:advanced-types/rt:feature-flags` (bits: logging, monitoring, debugging, security)

---

## Constraints (Contraintes)

Règles de validation appliquées aux données.

**Contraintes dans restconf-test.yang** :
- **mandatory** : `rt:basic-data/rt:device-id` (doit être présent)
- **range** : `rt:constraints/rt:range-test/rt:percentage-value` (0..100)
- **length** : `rt:constraints/rt:password` (8..64 caractères)
- **pattern** : `rt:basic-data/rt:device-id` (pattern pour noms de device)
- **must** : `rt:constraints/rt:must-test` (start-time < end-time)
- **when** : `rt:interfaces/rt:advanced-features/rt:advanced-interface` (si enable-advanced=true)
- **unique** : `rt:constraints/rt:unique-test/rt:items` (unique "name priority")
- **min-elements** : `rt:interfaces/rt:vlan` (min=1)
- **max-elements** : `rt:interfaces/rt:vlan` (max=4094)

---

## Operational State

Données qui représentent l'état actuel du système, marquées avec `config false`.

**Exemples dans restconf-test.yang** :
- `rt:system/rt:state` (container config false)
- `rt:basic-data/rt:uptime` (leaf config false)

Accès via : `/restconf/ds/ietf-datastores:operational/rt:restconf-test`

---

## Configuration Data

Données configurables par l'utilisateur, marquées avec `config true` (par défaut).

**Exemples dans restconf-test.yang** :
- `rt:basic-data/rt:device-id`
- `rt:basic-data/rt:timeout`
- `rt:interfaces/rt:interface`

Accès via : `/restconf/ds/ietf-datastores:running/rt:restconf-test`