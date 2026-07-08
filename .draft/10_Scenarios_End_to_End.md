# 10 - Matrice de conformité RFC

## 10.1 RFC8040

| Référence RFC | Exigence | Cas de test |
|---------------|----------|-------------|
| §3.1 | Découverte RESTCONF | TC-2-001 à TC-2-004 |
| §3.3 | Ressource /data | TC-3-001 |
| §4.2 | GET | TC-3-001 à TC-3-010 |
| §4.3 | POST | TC-3-011 à TC-3-017 |
| §4.4 | PUT | TC-3-018 à TC-3-021 |
| §4.5 | PATCH | TC-3-022 à TC-3-026 |
| §4.6 | DELETE | TC-3-027 à TC-3-031 |
| §4.8 | Paramètres de requête | TC-4-001 à TC-4-055 |
| §5 | RPC | TC-5-001 à TC-5-014 |
| §6 | Notifications | TC-5-020 à TC-5-040 |
| §7 | Erreurs | TC-7-001 à TC-7-067 |

---

## 10.2 RFC8527

| Référence RFC | Exigence | Cas de test |
|---------------|----------|-------------|
| §3 | Datastores | TC-6-001 à TC-6-023 |
| §5 | with-origin | TC-6-024 à TC-6-028 |
| §6 | Operational | TC-6-012 à TC-6-016 |

---

## 10.3 RFC7950

| Élément YANG | Cas de test |
|--------------|-------------|
| leaf | TC-3-004 |
| list | TC-3-005 |
| leaf-list | TC-3-008 |
| anydata | TC-3-009 |
| anyxml | TC-3-010 |
| mandatory | TC-7-014 |
| must | TC-7-031 |
| when | TC-7-032 |
| unique | TC-7-033 |
| leafref | TC-7-027 |
| identityref | TC-7-026 |
| union | TC-7-029 |
| bits | TC-7-021 |
| binary | TC-7-030 |