# Couverture fonctionnelle

## ⚠️ Module YANG de référence

**Module principal** : `restconf-test.yang` (namespace: `urn:restconf:test`, prefix: `rt`)

Toutes les fonctionnalités listées ci-dessous sont couvertes par le module `restconf-test.yang`, sauf indication contraire.

---

## Couverture des Fonctionnalités

| Fonction | Couverture | Module YANG | Détails |
|----------|------------|-------------|---------|
| HTTPS | ✔ | N/A | Infrastructure serveur |
| Authentification | ✔ | N/A | Contrôle d'accès |
| CRUD | ✔ | rt:restconf-test | /data, /ds/* |
| Query Parameters | ✔ | rt:restconf-test | content, depth, fields, filter, insert, point, with-defaults, with-origin |
| RPC | ✔ | rt:restconf-test | 6 RPCs disponibles |
| Actions | ✔ | rt:restconf-test | 3 Actions YANG (device-management) |
| Notifications | ✔ | rt:restconf-test | 4 Notifications avec données |
| NMDA | ✔ | rt:restconf-test | running, intended, operational |
| Sécurité | ✔ | rt:restconf-test | NACM, contrôle d'accès |
| Performance | ✔ | rt:restconf-test | Structures pour tests de charge |
| Robustesse | ✔ | rt:restconf-test | Contraintes, erreurs, concurrence |

---

## Couverture RFC8040

| Section | Couverture | Module YANG | URI/Opération |
|----------|------------|-------------|---------------|
| Discovery | ✔ | N/A | /.well-known/host-meta |
| API Root | ✔ | N/A | /restconf |
| Data | ✔ | rt:restconf-test | /restconf/data/rt:restconf-test |
| GET | ✔ | rt:restconf-test | Toutes les ressources |
| POST | ✔ | rt:restconf-test | Création listes (interface, vlan) |
| PUT | ✔ | rt:restconf-test | Remplacement complet |
| PATCH | ✔ | rt:restconf-test | Modification partielle |
| DELETE | ✔ | rt:restconf-test | Suppression |
| Query Parameters | ✔ | rt:restconf-test | content, depth, fields, etc. |
| RPC | ✔ | rt:restconf-test | /restconf/operations/rt:* |
| Notifications | ✔ | rt:restconf-test | /restconf/streams/rt:* |
| Errors | ✔ | rt:restconf-test | Tous les codes HTTP |

---

## Couverture RFC8527 (NMDA)

| Section | Couverture | Module YANG | URI |
|----------|------------|-------------|-----|
| Running | ✔ | rt:restconf-test | /restconf/ds/ietf-datastores:running/rt:restconf-test |
| Intended | ✔ | rt:restconf-test | /restconf/ds/ietf-datastores:intended/rt:restconf-test |
| Operational | ✔ | rt:restconf-test | /restconf/ds/ietf-datastores:operational/rt:restconf-test |
| with-origin | ⚠️ | rt:restconf-test | ?with-origin=true (nécessite support serveur) |

---

## Couverture RFC7950 (YANG 1.1)

| Elément | Couverture | Module YANG | Emplacement |
|----------|------------|-------------|------------|
| Container | ✔ | rt:restconf-test | rt:system, rt:basic-data, rt:interfaces, etc. |
| Presence Container | ✔ | rt:restconf-test | rt:system/rt:config |
| List | ✔ | rt:restconf-test | rt:interfaces/rt:interface, rt:interfaces/rt:vlan |
| Leaf | ✔ | rt:restconf-test | rt:basic-data/rt:device-id, etc. |
| Leaf-list | ✔ | rt:restconf-test | rt:access-control/rt:allowed-ips |
| Choice | ⚠️ | N/A | Non implémenté dans restconf-test.yang |
| Anydata | ✔ | rt:restconf-test | rt:custom-data/rt:vendor-specific |
| Anyxml | ✔ | rt:restconf-test | rt:custom-data/rt:legacy-config |
| MUST | ✔ | rt:restconf-test | rt:constraints/rt:must-test |
| WHEN | ✔ | rt:restconf-test | rt:interfaces/rt:advanced-features/rt:advanced-interface |
| Leafref | ✔ | rt:restconf-test | rt:advanced-types/rt:default-interface |
| Identityref | ✔ | rt:restconf-test | rt:advanced-types/rt:sensor-type-ref |
| Union | ✔ | rt:restconf-test | rt:advanced-types/rt:mixed-value |
| Pattern | ✔ | rt:restconf-test | rt:basic-data/rt:device-id |
| Range | ✔ | rt:restconf-test | rt:constraints/rt:range-test/rt:percentage-value |
| Length | ✔ | rt:restconf-test | rt:constraints/rt:password |
| Min-elements | ✔ | rt:restconf-test | rt:interfaces/rt:vlan (min=1) |
| Max-elements | ✔ | rt:restconf-test | rt:interfaces/rt:vlan (max=4094) |
| Default | ✔ | rt:restconf-test | rt:basic-data/rt:timeout (default=30) |
| Mandatory | ✔ | rt:restconf-test | rt:basic-data/rt:device-id |
| Config false | ✔ | rt:restconf-test | rt:basic-data/rt:uptime, rt:system/rt:state |
| Ordered-by | ✔ | rt:restconf-test | rt:interfaces/rt:ordered-queue |
| Unique | ✔ | rt:restconf-test | rt:constraints/rt:unique-test |

---

## Couverture RFC7951 (JSON Encoding)

| Elément | Couverture | Module YANG |
|----------|------------|-------------|
| JSON Encoding | ✔ | rt:restconf-test | Toutes les opérations |
| Module Qualified Names | ✔ | rt:restconf-test | Namespace urn:restconf:test |
| JSON Value Mapping | ✔ | rt:restconf-test | Tous les types |

---

## Couverture RFC6243 (with-defaults)

| Paramètre | Couverture | Module YANG |
|-----------|------------|-------------|
| trim | ✔ | rt:restconf-test | ?with-defaults=trim |
| explicit | ✔ | rt:restconf-test | ?with-defaults=explicit |
| report-all | ✔ | rt:restconf-test | ?with-defaults=report-all |
| report-all-tagged | ✔ | rt:restconf-test | ?with-defaults=report-all-tagged |