# 🗺️ Roadmap d'Implémentation - Backend RESTCONF h2c

> **⚠️ Rappel des Règles de Développement (CLAUDE.md)**
> *   **Transport** : HTTP/2 Cleartext (h2c) uniquement. Zéro TLS.
> *   **Architecture** : 100% Mono-thread, non-bloquant (`libevent` + pipe `sysrepo`).
> *   **Sécurité** : JWT validé via OpenSSL, clé publique extraite du Kernel Keyring via `syscall`.
> *   **Plugin** : Compilable en mode Interne (monolithique) ou Externe (IPC via UDS).
> *   **Médias** : `application/yang-data+json` et `application/yang-data+xml`.
> *   **Formatage** : Indentation par **TABULATIONS** (taille 8), lignes limitées à **80 caractères**.
> *   **API Sysrepo** : `SR_SUBSCR_NO_THREAD` obligatoire. `sr_acquire_context()` / `sr_release_context()`.

Les identifiants de tâche (`3.10`, `5.4`, ...) sont stables et référencés
depuis des commentaires `TODO`/`cf.` dans le code source : ne pas les
renuméroter en éditant ce fichier.

---

## 📊 Tableau de Bord Global

| Phase | Description | Progression | Statut |
| :--- | :--- | :---: | :---: |
| **1** | Fondations Réseau & Boucle d'Événements | 100% | 🟢 |
| **2** | Sécurité, JWT & NACM | 100% | 🟢 |
| **3** | Architecture Plugin Sysrepo (Dual-Mode) | 90% | 🟡 |
| **4** | Cœur RESTCONF & CRUD (RFC 8040) | 95% | 🟡 |
| **5** | Extensions NMDA (RFC 8527) | 100% | 🟢 |
| **6** | Notifications & SSE (RFC 8650) | 60% | 🟡 |
| **7** | Monitoring & Modules YANG Conceptuels | 50% | 🟡 |
| **8** | Tests h2c & Intégration CTest | 85% | 🟡 |

*(Légende : ⚪ À faire | 🟡 En cours | 🟢 Terminé ; `[x]` fait, `[~]` partiel, `[ ]` à faire)*

---

## 📋 Détail des Étapes par Phase

### Phase 1 : Fondations Réseau & Boucle d'Événements
*Objectif : Serveur TCP h2c non-bloquant et intégration des FD sysrepo.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 1.1 | Init `libevent` & TCP | - | `event_base_new()`, `evconnlistener` | `[x]` |
| 1.2 | Moteur HTTP/2 (h2c) | RFC 8040 Sec 2 | `nghttp2` mode Prior Knowledge | `[x]` |
| 1.3 | Intégration pipe sysrepo | - | `sr_get_event_pipe()` + `SR_SUBSCR_NO_THREAD` | `[x]` |
| 1.4 | Root Discovery | RFC 8040 Sec 3.1 | `GET /.well-known/host-meta` (XRD XML) | `[x]` |
| 1.5 | Mapping Headers HTTP/2 | RFC 8040 Sec 5 | `:method`, `:path`, `Authorization`, `Content-Type`, `Accept`, `Location` | `[x]` |
| 1.6 | Data Provider HTTP/2 | - | `nghttp2_data_provider` pour les réponses chunkées | `[x]` |

### Phase 2 : Sécurité, JWT & NACM
*Objectif : Authentification asynchrone sans thread.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 2.1 | Extraction clé Keyring | - | `syscall(__NR_request_key)` / `keyctl` | `[x]` |
| 2.2 | Base64URL Decode | RFC 7515 | `EVP_DecodeBlock` (OpenSSL) | `[x]` |
| 2.3 | Vérification JWT | RFC 7519 | `EVP_DigestVerify` en mémoire (CPU pur) | `[x]` |
| 2.4 | Mapping NACM | RFC 8040 Sec 4 | Claim `sub` → `sr_session_set_user()` | `[x]` |
| 2.5 | Gestion erreurs Auth | RFC 8040 Sec 7 | HTTP 401/403 + `ietf-restconf:errors` | `[x]` |
| 2.6 | Parsing JSON robuste du payload JWT | RFC 7519 | `jwt_validator.c` utilise **json-c** (`json_tokener_parse()`, `json_object_object_get_ex()`) au lieu d'un parser maison | `[x]` |

### Phase 3 : Architecture Plugin Sysrepo
*Objectif : Couche d'abstraction et abonnements sysrepo.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 3.1 | Config CMake Dual-Mode | - | Option `BUILD_EXTERNAL_PLUGIN` | `[x]` |
| 3.2 | Mode Interne | - | Liens statiques, appels C directs | `[x]` |
| 3.3 | Mode Externe (IPC UDS) — connexion | - | `evconnlistener` / `bufferevent` sur `AF_UNIX` des deux côtés (`uds_gateway.c`, `uds_plugin.c`) | `[x]` |
| 3.4 | Protocole IPC — framing | - | Framing Length-Header, magic `0x52434E46` (`uds_common.c`) | `[x]` |
| 3.5 | Contexte Libyang | - | `sr_acquire_context()` / `sr_release_context()` — mode interne uniquement ; en mode externe `plugin_acquire_ly_ctx()` renvoie `NULL` (cf. 3.10) | `[~]` |
| 3.6 | Callbacks Oper Data | YANG `rcmon` | `sr_oper_get_subscribe` (NO_THREAD) ; `oper_get_cb` génère dynamiquement capabilities et streams (cf. 7.1) | `[x]` |
| 3.7 | Callbacks RPC | YANG `rsn` | `sr_rpc_subscribe_tree` (NO_THREAD) ; `rpc_establish_sub_cb` génère un ID de souscription mais ne pousse aucune notification réelle (cf. 6.1) | `[~]` |
| 3.8 | Mode Externe — dispatch IPC réel | - | `plugin_data_cb`/`plugin_rpc_cb` transport-agnostiques (statut HTTP + corps déjà sérialisé) ; `uds_gateway.c` sérialise GET/EDIT/RPC vers l'UDS, `uds_plugin.c` désérialise et réutilise `plugin_handle_get/edit/rpc`. Corrélation par `msg_id`, framing par longueur, `fail_all_pending` sur déconnexion. `plugin_subscribe_notifications` reste un stub (dépend de 6.1) | `[x]` |
| 3.9 | Mode Externe — connexion sysrepo du démon | - | `plugin_main.c` appelle `plugin_init()` (`sr_connect()`, sessions running/operational/startup, abonnements) avant d'ouvrir l'UDS ; `plugin_destroy()` au shutdown | `[x]` |
| 3.10 | Mode Externe — contexte libyang à distance | - | `plugin_acquire_ly_ctx()` renvoie toujours `NULL` en mode externe → résolution des clés de liste désactivée dans `router.c` pour toute URI keyée. Reste à exposer `ly_ctx` via IPC, ou maintenir un contexte local synchronisé côté gateway | `[ ]` |
| 3.11 | `plugin_handle_rpc` en mode interne | RFC 8040 Sec 3.6 | Ajoutée à `sysrepo_plugin.c`, câblée sur `sr_rpc_send_tree()` (cf. 4.10) | `[x]` |

### Phase 4 : Cœur RESTCONF & CRUD (RFC 8040)
*Objectif : URI parsing, Codec, et opérations CRUD complètes.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 4.1 | Parseur URI RESTCONF | RFC 8040 Sec 3.5.3 | Conversion `list=key` → XPath via `libyang` | `[x]` |
| 4.2 | Percent-Encoding | RFC 8040 Sec 3.5.3 | Décodage des clés de listes (`%2C`, etc.) | `[x]` |
| 4.3 | Codec JSON/XML | RFC 8040 Sec 3.2, 5.2 | `lyd_print_mem()` / `lyd_parse_data_mem()` | `[x]` |
| 4.4 | Erreurs RESTCONF | RFC 8040 Sec 7.1 | `codec_serialize_errors()` JSON/XML (`yang-errors`) | `[x]` |
| 4.5 | Méthodes GET / HEAD | RFC 8040 Sec 4.2, 4.3 | `sr_get_data()` — appel synchrone, jugé acceptable car sysrepo utilise la SHM (cf. 7.5) | `[~]` |
| 4.6 | Méthodes POST / PUT | RFC 8040 Sec 4.4, 4.5 | `sr_edit_batch()` (merge/replace), `204 No Content`, header `Location` sur `201` | `[x]` |
| 4.7 | Méthode PATCH | RFC 8040 Sec 4.6 | `sr_edit_batch` (merge), Plain Patch | `[x]` |
| 4.8 | Méthode DELETE | RFC 8040 Sec 4.7 | `sr_delete_item()` | `[x]` |
| 4.9 | API Resource | YANG `ietf-restconf` | `GET /restconf` (data, operations, yang-library-version) | `[x]` |
| 4.10 | Invocation RPC / Action | RFC 8040 Sec 3.6 | `plugin_handle_rpc` parse le body input, appelle `sr_rpc_send_tree()`, encapsule l'output. `main.c` route vers `rpc_data_cb` (200/204/4xx/5xx) | `[x]` |
| 4.11 | Query: content | RFC 8040 Sec 4.8.1 | `config`/`nonconfig`/`all` via `SR_OPER_NO_STATE`/`SR_OPER_NO_CONFIG` dans `plugin_handle_get` | `[x]` |
| 4.12 | Query: depth | RFC 8040 Sec 4.8.2 | `req->depth` transmis comme `max_depth` à `sr_get_data()` (0 = illimité si absent/`unbounded`) | `[x]` |
| 4.13 | Query: fields | RFC 8040 Sec 4.8.3 | `codec_filter_fields()` filtre les nœuds de premier niveau selon `;`-séparés (basique — sous-chemins et parenthèses partiellement supportés, niveau racine uniquement) | `[x]` |
| 4.14 | ETag / Last-Modified | RFC 8040 Sec 3.4.1 | Collision prevention, `If-Match` — **prochaine étape recommandée**, cf. section "Dette Technique" | `[ ]` |
| 4.15 | Parsing Query String | RFC 8040 Sec 3.5.1 | `:path` HTTP/2 séparé en `path`/`query` dans `router_parse_request` ; extraction de `content`, `depth`, `fields`, `with-defaults`, `with-origin` | `[x]` |
| 4.16 | Sélection du datastore cible | RFC 8040 Sec 1.4, 3.4 | `/restconf/data` cible `SR_DS_RUNNING` par défaut ; sessions sysrepo pour running/operational/startup ; `select_session()` route vers la bonne session | `[x]` |

### Phase 5 : Extensions NMDA (RFC 8527)
*Objectif : Support des nouveaux datastores et métadonnées opérationnelles.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 5.1 | Routage `/ds/<datastore>` | RFC 8527 Sec 3.1 | `router.c` extrait l'`identityref` et mappe vers `RC_DS_RUNNING`/`RC_DS_OPERATIONAL`/`RC_DS_INTENDED` ; `sysrepo_plugin.c` sélectionne la session correspondante | `[x]` |
| 5.2 | Query: with-origin | RFC 8527 Sec 3.2.2 | Flag `SR_OPER_WITH_ORIGIN` passé à `sr_get_data()` quand `req->with_origin` est vrai et le datastore est `operational` | `[x]` |
| 5.3 | with-defaults sur Oper | RFC 8527 Sec 3.2.1 | `codec_serialize_data_wd()` mappe `report-all`/`report-all-tagged`/`trim`/`explicit` vers les flags libyang `LYD_PRINT_WD_*` | `[x]` |
| 5.4 | YANG Library 2019+ | RFC 8527 Sec 2 | `main.c` (`get_yang_library_revision()`) lit la révision réelle du module `ietf-yang-library` via `plugin_acquire_ly_ctx()` / `ly_ctx_get_module_implemented()` pour peupler `yang-library-version` (RFC 8040 §3.3.3), au lieu d'une chaîne littérale figée. Fallback sur `"2019-01-04"` si le contexte libyang est indisponible (mode Externe, cf. 3.10) ou si le module est introuvable. `GET /restconf/data/ietf-yang-library:yang-library` fonctionnait déjà nativement via `plugin_handle_get()`, sysrepo peuplant en interne les données opérationnelles de ce module | `[x]` |
| 5.5 | Opérations restreintes par datastore | RFC 8527 Sec 3.2 | `plugin_handle_edit` retourne `405`/`operation-not-supported` sur `operational` **et** `intended` (lecture seule) ; `400`/`invalid-value` pour toute identityref inconnue/dynamique (`RC_DS_UNKNOWN`) ; `main.c` mappe `SR_ERR_INVAL_ARG`→400, `SR_ERR_NOT_FOUND`→404, `SR_ERR_UNAUTHORIZED`→403 | `[x]` |

### Phase 6 : Notifications & SSE (RFC 8650)
*Objectif : Flux d'événements asynchrones sur streams HTTP/2.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 6.1 | RPC `establish-subscription` | YANG `rsn` | `rpc_establish_sub_cb` extrait le stream demandé, génère un ID unique, le retourne dans l'output. Câblage vers `sr_notif_subscribe()` + `sse_stream_push_event()` reste à faire — nécessite un registre des flux SSE actifs (cf. section "Dette Technique") | `[~]` |
| 6.2 | Ouverture Stream SSE | RFC 8040 Sec 6.3 | `h2c_sse_stream_open()` via `nghttp2_submit_response()` avec data provider SSE | `[x]` |
| 6.3 | Push Asynchrone | RFC 8040 Sec 6.4 | `h2c_sse_stream_push()` avec file d'attente + `NGHTTP2_ERR_DEFERRED` + `nghttp2_session_resume_data()` | `[x]` |
| 6.4 | Keep-Alive SSE | RFC 8040 Sec 6.4 | Timer libevent `EV_PERSIST` (30s) envoie `: ping\n\n` | `[x]` |
| 6.5 | Replay | RFC 8040 Sec 4.8.7 | `start-time` / `stop-time` — dépend de 6.1 | `[ ]` |

### Phase 7 : Monitoring & Modules YANG Conceptuels
*Objectif : Peuplement des modules `ietf-restconf` et `ietf-restconf-monitoring`.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 7.1 | Peuplement `rcmon` (Capabilities) | YANG `rcmon` | `oper_get_cb` génère dynamiquement les capacités (defaults, with-defaults, depth, fields, with-origin) | `[x]` |
| 7.2 | Peuplement `rcmon` (Streams) | YANG `rcmon` | `oper_get_cb` génère la liste des streams (NETCONF par défaut) avec accès XML/JSON | `[x]` |
| 7.3 | Limitation Ressources | RFC 8040 Sec 12 | `WINDOW_UPDATE` nghttp2, timeouts `libevent` | `[ ]` |
| 7.4 | Tests Conformité | - | Validation RFC 8040/8527 avec `nghttp` | `[ ]` |
| 7.5 | Audit Mono-Thread | - | Zéro `pthread` confirmé. `sr_get_data()`/`sr_apply_changes()` restent synchrones (SHM sysrepo, appels rapides) — écart mineur avec la préférence `sr_get_data_async()` de `CLAUDE.md`, jugé acceptable | `[~]` |

### Phase 8 : Tests h2c & Intégration CTest
*Objectif : Suite de tests fonctionnelle et intégration CI.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 8.1 | Client h2c pour tests | - | `test/conftest.py` : `H2cClient` basé sur `h2` (hyper-h2), HTTP/2 Cleartext Prior Knowledge sur TCP. GET/POST/PUT/PATCH/DELETE/OPTIONS/HEAD | `[x]` |
| 8.2 | Tests Root Discovery | RFC 8040 Sec 3.1 | `test_basic.py::TestRootDiscovery` : `/.well-known/host-meta` (XRD XML) et `.json` | `[x]` |
| 8.3 | Tests API Resource | RFC 8040 Sec 3.2 | `test_basic.py::TestAPIResource` : `GET /restconf` en JSON et XML | `[x]` |
| 8.4 | Tests méthodes HTTP | RFC 8040 Sec 3.4 | HEAD, OPTIONS, DELETE sur `/restconf` (405) | `[x]` |
| 8.5 | Tests gestion erreurs | RFC 8040 Sec 7 | 9/12 tests passent. 3 échecs : crash serveur sur `DELETE /restconf` et `GET /restconf/data/%%%invalid` (`ConnectionRefusedError`), et `GET /restconf/data/unknown-module:unknown-node` sans réponse (`status_code=None`). Validation percent-encoding présente dans `router.c` mais crashes persistent | `[~]` |
| 8.6 | Tests CRUD data | RFC 8040 Sec 4 | GET/POST/PUT/PATCH/DELETE sur `/restconf/data/...` | `[ ]` |
| 8.7 | Tests NMDA | RFC 8527 | GET sur `/restconf/ds/<datastore>/...` | `[ ]` |
| 8.8 | Tests RPC/Action | RFC 8040 Sec 3.6 | POST sur `/restconf/operations/...` | `[ ]` |
| 8.9 | Intégration CTest | - | `enable_testing()` + `add_test()` dans `CMakeLists.txt` pour `ctest --output-on-failure` | `[ ]` |

---

## ⚠️ Dette Technique Connue

Points encore ouverts (l'historique des corrections déjà appliquées vit
dans les colonnes "Détails Techniques" ci-dessus et dans `git log`) :

| Sujet | Item(s) | Fichier(s) | Impact |
| :--- | :---: | :--- | :--- |
| Pas d'ETag / Last-Modified / If-Match (collision prevention) | 4.14 | `main.c`, `plugin_api.h`, `h2c_server.c` | Pas de détection de conflit d'édition concurrente (RFC 8040 §3.4.1) |
| Notifications sysrepo non câblées vers les flux SSE | 3.7, 6.1, 6.5 | `sysrepo_plugin.c`, `sse_stream.c`, `main.c` | `establish-subscription` répond avec un ID mais aucun événement n'est jamais poussé ; pas de registre des flux SSE actifs |
| Contexte libyang indisponible en mode Externe | 3.5, 3.10 | `uds_gateway.c`, `router.c` | Toute URI RESTCONF avec clé de liste échoue au parsing en mode Externe (IPC) |
| `plugin_subscribe_notifications` stub côté gateway externe | 3.8 (partiel) | `uds_gateway.c` | Dépend de 6.1 |
| Pas de limitation de ressources | 7.3 | `h2c_server.c` | Pas de `WINDOW_UPDATE` nghttp2 ni de timeouts `libevent` explicites |
| Couverture de tests incomplète | 7.4, 8.6–8.9 | `test/` | Pas de tests CRUD/NMDA/RPC, pas d'intégration `ctest` dans `CMakeLists.txt` |
| 3 tests d'erreurs en échec | 8.5 | `test/test_basic.py` | Crash serveur sur `DELETE /restconf` et sur une URI percent-encodée invalide |
| Fichier résiduel versionné `.CLAUDE.md.swp` | - | racine du dépôt | `.gitignore` corrigé pour l'avenir, mais le fichier déjà suivi doit être retiré manuellement : `git rm -f .CLAUDE.md.swp` |

---

## 🎯 Prochaines Étapes (par priorité)

1. **4.14 — ETag / Last-Modified / If-Match** (RFC 8040 §3.4.1)
   Étendre `plugin_data_cb`/`plugin_edit_cb` (ou ajouter des accesseurs
   dédiés dans `plugin_api.h`) pour exposer un entity-tag et un
   timestamp de dernière modification du datastore `running`, en mode
   Interne **et** Externe (IPC).

2. **6.1 — Câblage des notifications sysrepo réelles vers SSE**
   `plugin_subscribe_notifications` doit s'abonner via
   `sr_notif_subscribe()`, sérialiser l'événement reçu et le pousser
   sur les flux SSE actifs. Nécessite un registre des flux ouverts
   (actuellement les `sse_stream_t*` créés dans `main.c` ne sont pas
   conservés après `sse_stream_create()`).

3. **3.10 — Contexte libyang à distance en mode Externe**
   Exposer `ly_ctx` via IPC, ou maintenir un contexte local synchronisé
   côté gateway, pour débloquer la résolution des clés de liste.

4. **6.5 — Replay des notifications** (`start-time`/`stop-time`),
   dépend de 6.1.

5. **7.3 — Limitation de ressources** (`WINDOW_UPDATE` nghttp2,
   timeouts `libevent`).

6. **7.4, 8.6–8.9 — Tests** : conformité RFC 8040/8527, CRUD/NMDA/RPC,
   intégration `ctest`.

7. **8.5 — Corriger les 3 tests d'erreurs en échec** (crash serveur sur
   `DELETE /restconf` et sur URI percent-encodée invalide).

8. **Ménage** : retirer `.CLAUDE.md.swp` du suivi git.
