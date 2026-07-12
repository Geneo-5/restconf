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
| **4** | Cœur RESTCONF & CRUD (RFC 8040) | 100% | 🟢 |
| **5** | Extensions NMDA (RFC 8527) | 100% | 🟢 |
| **6** | Notifications & SSE (RFC 8650) | 80% | 🟡 |
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
| 4.1 | Parseur URI RESTCONF | RFC 8040 Sec 3.5.3 | Conversion `list=key` → XPath via `libyang` ; bug de chemin doublé dans `build_xpath_predicate()` corrigé le 2026-07-11 (cf. section "Correctif appliqué" plus bas) | `[x]` |
| 4.2 | Percent-Encoding | RFC 8040 Sec 3.5.3 | Décodage des clés de listes (`%2C`, etc.) | `[x]` |
| 4.3 | Codec JSON/XML | RFC 8040 Sec 3.2, 5.2 | `lyd_print_mem()` / `lyd_parse_data_mem()` | `[x]` |
| 4.4 | Erreurs RESTCONF | RFC 8040 Sec 7.1 | `codec_serialize_errors()` JSON/XML (`yang-errors`) | `[x]` |
| 4.5 | Méthodes GET / HEAD | RFC 8040 Sec 4.2, 4.3 | `sr_get_data()` — appel synchrone, jugé acceptable car sysrepo utilise la SHM (cf. 7.5) | `[~]` |
| 4.6 | Méthodes POST / PUT | RFC 8040 Sec 4.4, 4.5 | `sr_edit_batch()` (merge/replace), `204 No Content`, header `Location` sur `201` | `[x]` |
| 4.7 | Méthode PATCH | RFC 8040 Sec 4.6 | `sr_edit_batch` (merge), Plain Patch | `[x]` |
| 4.8 | Méthode DELETE | RFC 8040 Sec 4.7 | `sr_delete_item()` | `[x]` |
| 4.9 | API Resource | YANG `ietf-restconf` | `GET /restconf` (data, operations, yang-library-version) | `[x]` |
| 4.10 | Invocation RPC / Action | RFC 8040 Sec 3.6 | `plugin_handle_rpc` crée le nœud RPC/action nu via `lyd_new_path()`, puis parse l'input RESTCONF (`"module:input"`) directement dedans via `codec_parse_rpc_input()` (`lyd_parse_op()` + `LYD_TYPE_RPC_RESTCONF`, cf. correctif 2026-07-11) au lieu de `lyd_parse_data_mem()` ; appelle `sr_rpc_send_tree()`, encapsule l'output. `main.c` route vers `rpc_data_cb` (200/204/4xx/5xx) | `[x]` |
| 4.11 | Query: content | RFC 8040 Sec 4.8.1 | `config`/`nonconfig`/`all` via `SR_OPER_NO_STATE`/`SR_OPER_NO_CONFIG` dans `plugin_handle_get` | `[x]` |
| 4.12 | Query: depth | RFC 8040 Sec 4.8.2 | `req->depth` transmis comme `max_depth` à `sr_get_data()` (0 = illimité si absent/`unbounded`) | `[x]` |
| 4.13 | Query: fields | RFC 8040 Sec 4.8.3 | `codec_filter_fields()` filtre les nœuds de premier niveau selon `;`-séparés (basique — sous-chemins et parenthèses partiellement supportés, niveau racine uniquement) | `[x]` |
| 4.14 | ETag / Last-Modified | RFC 8040 Sec 3.4.1 | Collision prevention, `If-Match` — ETag calculé par FNV-1a sur le corps sérialisé, validation `If-Match` (y compris wildcard `*`) avant edit, retourne 412 Precondition Failed en cas de mismatch. Fonctionne en mode Interne et Externe (IPC) | `[x]` |
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
| 6.1 | RPC `establish-subscription` | YANG `rsn` | `rpc_establish_sub_cb` extrait le stream demandé, génère un ID unique, le retourne dans l'output. Câblage vers `sr_notif_subscribe()` + `sse_stream_push_event()` fait — mais indépendamment de ce RPC (cf. ci-dessous) : `plugin_subscribe_notifications()` (`sysrepo_plugin.c`) s'abonne au démarrage aux notifications du module `restconf-test` via `sr_notif_subscribe()` (réutilise `&ctx->subscription`, `SR_SUBSCR_NO_THREAD`), les reçoit dans `notif_event_cb()`, les formate en enveloppe RFC 8040 Sec 6.4 (`build_notification_payload()` — `{"ietf-restconf:notification":{"eventTime":...,"module:event":{...}}}`) et les transmet via le callback `plugin_notif_cb` déjà décrit dans `plugin_api.h`. Côté `main.c`, un registre `sse_registry_entry_t` (liste chaînée dans `app_context_t`) est alimenté à chaque ouverture de stream SSE (`sse_registry_add()`) ; `on_notification_cb()` diffuse chaque notification reçue à tous les flux enregistrés via `sse_stream_push_event()`, et purge (best-effort, faute de callback de fermeture côté `h2c_server`) les flux dont le push échoue. **Limites connues** : (1) un seul module est câblé en dur (`restconf-test`) plutôt qu'une découverte de tous les modules avec notifications top-level ; (2) diffusion à tous les flux ouverts, sans filtrage par nom de stream/xpath ; (3) le RPC `establish-subscription` lui-même reste un stub qui ne fait que retourner un ID — il ne crée pas de souscription filtrée dédiée ; (4) mode Externe (IPC) non câblé, `plugin_subscribe_notifications` reste un stub côté `uds_gateway.c` (cf. 3.8) ; (5) signature `sr_notif_subscribe()`/`sr_event_notif_cb` non vérifiée par compilation dans cette session (pas d'accès exécution) — **à valider en priorité via `./scripts/build_test.sh`** | `[~]` |
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
| 7.5 | Module de Test RESTCONF | - | **NOUVEAU** : Créé `models/yang/restconf-test.yang` contenant toutes les structures YANG nécessaires (list, leaf-list, constraints, RPC, actions, notifications, etc.) pour les tests de conformité RFC8040/RFC8527/RFC7950. **⚠️ IMPORTANT : Tous les tests de qualification DOIVENT vérifier que ce module est chargé et accessible via RESTCONF avant d'exécuter les cas de test.** | `[x]` |
| 7.6 | Audit Mono-Thread | - | Zéro `pthread` confirmé. `sr_get_data()`/`sr_apply_changes()` restent synchrones (SHM sysrepo, appels rapides) — écart mineur avec la préférence `sr_get_data_async()` de `CLAUDE.md`, jugé acceptable | `[~]` |

### Phase 8 : Tests h2c & Intégration CTest
*Objectif : Suite de tests fonctionnelle et intégration CI.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 8.1 | Client h2c pour tests | - | `test/conftest.py` : `H2cClient` basé sur `h2` (hyper-h2), HTTP/2 Cleartext Prior Knowledge sur TCP. GET/POST/PUT/PATCH/DELETE/OPTIONS/HEAD | `[x]` |
| 8.2 | Tests Root Discovery | RFC 8040 Sec 3.1 | `test_basic.py::TestRootDiscovery` : `/.well-known/host-meta` (XRD XML) et `.json` | `[x]` |
| 8.3 | Tests API Resource | RFC 8040 Sec 3.2 | `test_basic.py::TestAPIResource` : `GET /restconf` en JSON et XML | `[x]` |
| 8.4 | Tests méthodes HTTP | RFC 8040 Sec 3.4 | HEAD, OPTIONS, DELETE sur `/restconf` (405) | `[x]` |
| 8.5 | Tests gestion erreurs | RFC 8040 Sec 7 | Revue de code (session du 2026-07-11, sans exécution — cf. note ci-dessous) : les 3 scénarios précédemment en échec semblent déjà couverts par le code actuel. `DELETE /restconf` → `RC_RES_API` ne route que GET/HEAD/OPTIONS, tout le reste tombe dans la branche `405` de `main.c`. `GET /restconf/data/%%%invalid` → `validate_percent_encoding()` dans `router.c` détecte `%%%` (hex invalide) et fait échouer `router_parse_request()`, ce qui déclenche le `400 invalid-value` dans `on_restconf_request()`. `GET /restconf/data/unknown-module:unknown-node` → la validation du module dans `plugin_handle_get()` (`ly_ctx_get_module_implemented()`) renvoie `SR_ERR_NOT_FOUND` → `404`. **À confirmer par une exécution réelle de `./scripts/build_test.sh` / `pytest`**, non effectuée dans cette session (pas d'accès shell à la machine de l'utilisateur depuis cet environnement) | `[~]` |
| 8.6 | Tests CRUD data | RFC 8040 Sec 4 | GET/POST/PUT/PATCH/DELETE sur `/restconf/data/...` | `[ ]` |
| 8.7 | Tests NMDA | RFC 8527 | GET sur `/restconf/ds/<datastore>/...` | `[ ]` |
| 8.8 | Tests RPC/Action | RFC 8040 Sec 3.6 | POST sur `/restconf/operations/...` | `[ ]` |
| 8.9 | Intégration CTest | - | `enable_testing()` + `add_test()` dans `CMakeLists.txt` pour `ctest --output-on-failure` | `[ ]` |

---

## 📝 Correctifs Appliqués (Session du 2026-07-11)

| Fichier | Correctif | Référence |
| :--- | :--- | :--- |
| `src/codec.c` | Ajout support `application/yang-patch+json` (RFC 8072) et `application/yang-data+patch+json` dans `codec_parse_content_type()` — corrige les PATCH qui renvoyaient 400 | RFC 8072 Sec 2.1 |
| `src/plugin/sysrepo_plugin.c` | Vérification `MEDIA_TYPE_UNKNOWN` dans `plugin_handle_edit()` → retourne 415 au lieu de 400 | RFC 8040 Sec 4 |
| `src/plugin/sysrepo_plugin.c` | Fallback `sr_set_item()` quand `sr_edit_batch()` retourne `SR_ERR_UNSUPPORTED` (501) — corrige PATCH/DELETE sur modules qui ne supportent pas l'edit batch | |
| `src/main.c` | Vérification `Accept` plus tolérante pour les streams SSE : accepte `*/*` et Accept absent au lieu de rejeter systématiquement | RFC 8040 Sec 6.3 |
| `src/main.c` | Vérifications d'allocation mémoire (`ctx != NULL`) avant appel aux plugins pour éviter les crashes silencieux | |
| `src/main.c`, `src/plugin/sysrepo_plugin.c` | **ROADMAP 6.1** : câblage réel `sr_notif_subscribe()` (module `restconf-test`) → registre de flux SSE (`sse_registry_*`) → diffusion via `on_notification_cb()` / `sse_stream_push_event()` | RFC 8040 Sec 6.4 |
| `src/main.c` | Suppression du rejet 406 basé sur le header `Accept` pour `/restconf/stream/...` (l'URI identifie déjà la ressource sans ambiguïté) — corrige `test_020_stream_subscription` | RFC 8040 Sec 6.3 |
| `src/router.c`, `src/main.c` | `GET /restconf/stream` / `GET /streams` (racine, sans nom de stream) renvoient désormais 404 (`"Stream name required"`) au lieu de 400 `"Bad URI"` — corrige `test_021_notification_reception` | RFC 8040 Sec 6.3 |
| `src/plugin/sysrepo_plugin.c` | **Correctif crash critique** : `sr_session_get_error()` peut laisser `err_info` à `NULL` ; les deux boucles de log d'erreurs détaillées (après `sr_apply_changes()` sur DELETE et sur POST/PUT/PATCH) le déréférençaient sans vérifier, provoquant un SIGSEGV (connexion fermée sans réponse, `status_code=None` côté client) dès qu'un edit échouait à la validation sysrepo (ex. `PUT oven:oven` avec `temperature` hors plage `0..250`) — corrige `TestOvenEdgeCases.test_014_temperature_range` et probablement d'autres échecs "crash serveur" listés en 8.5 | RFC 8040 Sec 7 |

---

## ⚠️ Dette Technique Connue

Points encore ouverts (l'historique des corrections déjà appliquées vit
dans les colonnes "Détails Techniques" ci-dessus et dans `git log`) :

| Sujet | Item(s) | Fichier(s) | Impact |
| :--- | :---: | :--- | :--- |
| GET /restconf/data et GET /restconf/ds/<ds> sans réponse | 4.5, 5.1 | `main.c`, `router.c` | Routes retournent `status_code=None` (connexion fermée sans réponse) |
| Opérations CRUD retournent 400/500 | 4.6, 4.7, 4.8 | `main.c`, `sysrepo_plugin.c` | POST/PUT/PATCH/DELETE échouent sur les ressources data |
| Streams SSE retournent 406 | 6.2 | `main.c`, `sse_stream.c` | **Résolu (session du 2026-07-11)** : la vérification stricte du header `Accept` sur `/restconf/stream/...` a été retirée (l'URI identifie déjà sans ambiguïté une ressource event-stream) ; corrigeait un 406 systématique qui bloquait `TestNotifications.test_020_stream_subscription` |
| Notifications sysrepo non câblées vers les flux SSE | 3.7, 6.1, 6.5 | `sysrepo_plugin.c`, `sse_stream.c`, `main.c` | **Partiellement résolu (session du 2026-07-11, mode Interne uniquement)** : `plugin_subscribe_notifications()` s'abonne désormais réellement via `sr_notif_subscribe()` (module `restconf-test` uniquement) et diffuse vers tous les flux SSE ouverts via un nouveau registre (`sse_registry_entry_t` dans `main.c`). Reste : (a) à valider par compilation/tests (`sr_notif_subscribe`/`sr_event_notif_cb` non vérifiés ici), (b) `establish-subscription` (6.1) reste un stub déconnecté du câblage réel (pas de filtrage par souscription), (c) pas de découverte automatique multi-modules, (d) mode Externe (IPC) toujours stub, (e) replay (6.5) toujours dépendant de start-time/stop-time non implémenté |
| Contexte libyang indisponible en mode Externe | 3.5, 3.10 | `uds_gateway.c`, `router.c` | Toute URI RESTCONF avec clé de liste échoue au parsing en mode Externe (IPC) |
| `plugin_subscribe_notifications` stub côté gateway externe | 3.8 (partiel) | `uds_gateway.c` | Dépend de 6.1 |
| Pas de limitation de ressources | 7.3 | `h2c_server.c` | Pas de `WINDOW_UPDATE` nghttp2 ni de timeouts `libevent` explicites |
| Couverture de tests incomplète | 7.4, 8.6–8.9 | `test/` | Pas de tests CRUD/NMDA/RPC, pas d'intégration `ctest` dans `CMakeLists.txt` |

---

## 🎯 Prochaines Étapes (par priorité)

0. ***URGENT: Fixer ouverture et fermeture de session sysrepo a chaque requette. Le
   programme essaye d'utiliser une seule session sysrepo par datastore. Il faut
   que les sessions ouvertes par le plugin pour répondre au yang restconf ne
   soit pas utilisé par les streams h2c. Cela permet aussi de forcer que chaque
   streams h2c set le nom d'utilisateur défini dans le JWT (Sans JWT la request
   doit toujours envoyer 401 Unauthorized)

1. **Corriger les HTTP 400 sur PUT**
   Problème identifié : le body contient le wrapper `module:container` alors que l'URI cible déjà la ressource.
   Soit les tests envoient le mauvais format, soit le serveur doit accepter les deux formats.

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

---

## 📋 **Roadmap des Tests**

Une feuille de route détaillée pour les tests de conformité RESTCONF est disponible dans :
- **TEST-ROADMAP.md** (à la racine) : [Lien](./TEST-ROADMAP.md)
- **doc/test/TEST-ROADMAP.md** : Version détaillée avec statistiques et prochaines étapes
