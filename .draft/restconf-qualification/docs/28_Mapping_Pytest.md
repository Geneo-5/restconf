| ID Test | Pytest | Dataset | RFC | Scénario |
|---|---|---|---|---|
| RESTCONF-INF-001 | test_restconf_root.py | - | RFC8040 | SQ-OVEN-001 |
| RESTCONF-YANG-001 | test_yang_library.py | - | RFC8525 | SQ-OVEN-001 |
| RESTCONF-CRUD-001 | test_get_oven.py | oven_off.json | RFC8040 §4 | SQ-OVEN-001 |
| RESTCONF-CRUD-002 | test_patch_temperature.py | oven_180.json | RFC8040 §4.5 | SQ-OVEN-001 |
| RESTCONF-YANG-010 | test_invalid_temperature.py | oven_251.json | RFC7950 | SQ-OVEN-002 |
| RESTCONF-RPC-001 | test_insert_food.py | insert_food.json | RFC8040 | SQ-OVEN-003 |
| RESTCONF-RPC-002 | test_remove_food.py | remove_food.json | RFC8040 | SQ-OVEN-004 |
| RESTCONF-NOTIF-001 | test_oven_ready.py | - | RFC8040 | SQ-OVEN-005 |