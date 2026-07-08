# Matrice de conformité

## ⚠️ Module YANG de référence

**Module à utiliser pour tous les tests** : `restconf-test.yang` (namespace: `urn:restconf:test`, prefix: `rt`)

Vérifier que le module est chargé avant d'exécuter les tests :
```bash
GET /restconf/data/rt:restconf-test
```

---

## Matrice RFC → Cas de Test

| ID | RFC | Section | Description | Cas de test | Module YANG | Statut |
|----|-----|----------|-------------|-------------|-------------|--------|
| REQ-001 | 8040 | 3.1 | Découverte RESTCONF | TC-2-001 à TC-2-004 | N/A (infrastructure) | ✔ |
| REQ-002 | 8040 | 3.3 | Ressource /data | TC-3-001 | rt:restconf-test | ✔ |
| REQ-003 | 8040 | 4.2 | GET | TC-3-001 à TC-3-010 | rt:restconf-test | ✔ |
| REQ-004 | 8040 | 4.3 | POST | TC-3-011 à TC-3-017 | rt:restconf-test | ✔ |
| REQ-005 | 8040 | 4.4 | PUT | TC-3-018 à TC-3-021 | rt:restconf-test | ✔ |
| REQ-006 | 8040 | 4.5 | PATCH | TC-3-022 à TC-3-026 | rt:restconf-test | ✔ |
| REQ-007 | 8040 | 4.6 | DELETE | TC-3-027 à TC-3-031 | rt:restconf-test | ✔ |
| REQ-008 | 8040 | 4.8 | depth | TC-4-006 à TC-4-013 | rt:restconf-test | ✔ |
| REQ-009 | 8040 | 4.8 | fields | TC-4-014 à TC-4-025 | rt:restconf-test | ✔ |
| REQ-010 | 8040 | 4.8 | filter | TC-4-021 à TC-4-025 | rt:restconf-test | ✔ |
| REQ-011 | 8040 | 4.8 | insert | TC-4-031 à TC-4-036 | rt:restconf-test | ✔ |
| REQ-012 | 8040 | 4.8 | point | TC-4-037 à TC-4-039 | rt:restconf-test | ✔ |
| REQ-013 | 8040 | 4.8 | with-defaults | TC-4-026 à TC-4-030 | rt:restconf-test | ✔ |
| REQ-014 | 6243 | - | with-defaults | TC-4-026 à TC-4-030 | rt:restconf-test | ✔ |
| REQ-015 | 8527 | 5 | with-origin | TC-4-040 à TC-4-042, TC-6-024 à TC-6-028 | rt:restconf-test | ⚠️ |
| REQ-016 | 8040 | 5 | RPC | TC-5-001 à TC-5-014 | rt:restconf-test (6 RPCs) | ✔ |
| REQ-017 | 8040 | 6 | Notifications | TC-5-020 à TC-5-040 | rt:restconf-test (4 notifications) | ✔ |
| REQ-018 | 8527 | 3 | NMDA Datastores | TC-6-001 à TC-6-032 | rt:restconf-test | ✔ |
| REQ-019 | 7950 | 7.5.4 | Presence Container | TC-3-003 | rt:restconf-test/rt:system/rt:config | ✔ |
| REQ-020 | 7950 | 7.6 | Mandatory | TC-3-039, TC-7-014 | rt:restconf-test/rt:basic-data/rt:device-id | ✔ |
| REQ-021 | 7950 | 7.7 | List | TC-3-005, TC-3-006, TC-3-011, TC-7-036-041 | rt:restconf-test/rt:interfaces/rt:interface | ✔ |
| REQ-022 | 7950 | 7.8 | Leaf-list | TC-3-008 | rt:restconf-test/rt:access-control/rt:allowed-ips | ✔ |
| REQ-023 | 7950 | 7.10 | Anydata | TC-3-009, TC-7-043 | rt:restconf-test/rt:custom-data/rt:vendor-specific | ✔ |
| REQ-024 | 7950 | 7.11 | Anyxml | TC-3-010, TC-7-044, TC-7-045 | rt:restconf-test/rt:custom-data/rt:legacy-config | ✔ |
| REQ-025 | 7950 | 7.15 | RPC | TC-5-001-014 | rt:restconf-test (6 RPCs) | ✔ |
| REQ-026 | 7950 | 7.16 | Notification | TC-5-020-040 | rt:restconf-test (4 notifications) | ✔ |
| REQ-027 | 7950 | 9.6 | Union | TC-7-029 | rt:restconf-test/rt:advanced-types/rt:mixed-value | ✔ |
| REQ-028 | 7950 | 9.7 | Bits | TC-7-021 | rt:restconf-test/rt:advanced-types/rt:feature-flags | ✔ |
| REQ-029 | 7950 | 9.8 | Decimal64 | TC-7-022 | rt:restconf-test/rt:advanced-types/rt:precision-value | ✔ |
| REQ-030 | 7950 | 9.9 | Binary | TC-7-030 | rt:restconf-test/rt:advanced-types/rt:certificate | ✔ |
| REQ-031 | 7950 | 9.10 | Identityref | TC-7-026 | rt:restconf-test/rt:advanced-types/rt:sensor-type-ref | ✔ |
| REQ-032 | 7950 | 9.11 | Leafref | TC-3-048, TC-7-027 | rt:restconf-test/rt:advanced-types/rt:default-interface | ✔ |
| REQ-033 | 7950 | 7.5.3 | Must | TC-3-050, TC-7-031 | rt:restconf-test/rt:constraints/rt:must-test | ✔ |
| REQ-034 | 7950 | 7.5.3 | When | TC-3-049, TC-7-032 | rt:restconf-test/rt:interfaces/rt:advanced-features | ✔ |
| REQ-035 | 7950 | 7.5.3 | Unique | TC-7-033 | rt:restconf-test/rt:constraints/rt:unique-test | ✔ |

---

## Légende

- ✔ = Applicable avec `restconf-test.yang`
- ⚠️ = Dépend du support serveur (ex: `with-origin` nécessite RFC8527 implémentée)
- Module YANG : `rt` = `urn:restconf:test` (restconf-test.yang)
- Prefix : `rt:` doit être utilisé dans toutes les requêtes RESTCONF

---

## Vérification pré-test

Avant d'exécuter chaque cas de test, vérifier :

1. Module chargé : `GET /restconf/data/rt:restconf-test` (doit retourner HTTP 200)
2. RPC disponibles : `GET /restconf/operations/rt:*` (doit lister les 6 RPCs)
3. Streams disponibles : `GET /restconf/streams/rt:*` (doit lister les 4 notifications)
4. Datastores (RFC8527) : `GET /restconf/ds/ietf-datastores:running/rt:restconf-test`
