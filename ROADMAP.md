# 🗺️ Roadmap d'Implémentation - Backend RESTCONF h2c

> **⚠️ Rappel des Règles de Développement (CLAUDE.md)**
> *   **Transport** : HTTP/2 Cleartext (h2c) uniquement. Zéro TLS.
> *   **Architecture** : 100% Mono-thread, non-bloquant (`libevent` + pipe `sysrepo`).
> *   **Sécurité** : JWT validé via OpenSSL, clé publique extraite du Kernel Keyring via `syscall`.
> *   **Plugin** : Compilable en mode Interne (monolithique) ou Externe (IPC via UDS).
> *   **Médias** : `application/yang-data+json` et `application/yang-data+xml`.
> *   **Formatage** : Indentation par **TABULATIONS** (taille 8), lignes limitées à **80 caractères**.
> *   **API Sysrepo** : `SR_SUBSCR_NO_THREAD` obligatoire. `sr_acquire_context()` / `sr_release_context()`.

> **📝 Note d'audit (relecture RFC 8040 / RFC 8527)**
> Une relecture croisée code/roadmap a révélé plusieurs statuts trop optimistes
> (session sysrepo figée sur `SR_DS_OPERATIONAL`, absence totale de parsing de
> query string, appels sysrepo bloquants). Les statuts ci-dessous ont été
> corrigés en conséquence — voir Phase 4, 5 et 7 et le nouveau tableau
> "Constats d'Audit" en fin de document.

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

*(Légende : ⚪ À faire | 🟡 En cours | 🟢 Terminé)*

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
| 2.2 | Base64URL Decode | RFC 7515 | Conversion + `EVP_DecodeBlock` (OpenSSL) | `[x]` |
| 2.3 | Vérification JWT | RFC 7519 | `EVP_DigestVerify` en mémoire (CPU pur) | `[x]` |
| 2.4 | Mapping NACM | RFC 8040 Sec 4 | Claim `sub` -> `sr_session_set_user()` | `[x]` |
| 2.5 | Gestion erreurs Auth | RFC 8040 Sec 7 | HTTP 401/403 + `ietf-restconf:errors` | `[x]` |
| 2.6 | Parsing JSON robuste du payload JWT | RFC 7519 | `jwt_validator.c` utilise désormais la bibliothèque **json-c** (`json_tokener_parse()`, `json_object_object_get_ex()`, `json_object_get_string()`) pour un parsing JSON robuste et maintenable. Remplace l'ancien parser maison. | `[x]` |

### Phase 3 : Architecture Plugin Sysrepo
*Objectif : Couche d'abstraction et abonnements sysrepo.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 3.1 | Config CMake Dual-Mode | - | Option `BUILD_EXTERNAL_PLUGIN` | `[x]` |
| 3.2 | Mode Interne | - | Liens statiques, appels C directs | `[x]` |
| 3.3 | Mode Externe (IPC UDS) — connexion | - | `evconnlistener` / `bufferevent` sur `AF_UNIX` établis des deux côtés (`uds_gateway.c`, `uds_plugin.c`) | `[x]` |
| 3.4 | Protocole IPC — framing | - | Framing Length-Header, magic `0x52434E46`, `ipc_serialize_request()`/`ipc_parse_header()` implémentés dans `uds_common.c` | `[x]` |
| 3.5 | Contexte Libyang | - | `sr_acquire_context()` / `sr_release_context()` (mode interne uniquement ; en mode externe `plugin_acquire_ly_ctx()` renvoie toujours `NULL`, cf. 3.9) | `[~]` |
| 3.6 | Callbacks Oper Data | YANG `rcmon` | `sr_oper_get_subscribe` (NO_THREAD) — souscription posée mais callback vide, cf. 7.1 | `[~]` |
| 3.7 | Callbacks RPC | YANG `rsn` | `sr_rpc_subscribe_tree` (NO_THREAD) — souscription posée mais callback vide, cf. 6.1 | `[~]` |
| 3.8 | **Mode Externe — dispatch IPC réel** | - | **Implémenté** : `plugin_data_cb`/`plugin_rpc_cb` redéfinis en callbacks transport-agnostiques (statut HTTP + corps déjà sérialisé), permettant à `uds_gateway.c` de sérialiser GET/EDIT/RPC vers l'UDS (format binaire privé `uds_data_proto.h`) et à `uds_plugin.c` de désérialiser puis réutiliser tel quel `plugin_handle_get/edit/rpc` de `sysrepo_plugin.c`. Corrélation par `msg_id`, framing par longueur, gestion de déconnexion (`fail_all_pending`). `plugin_subscribe_notifications` reste un stub (dépend de 6.1) | `[x]` |
| 3.9 | **Mode Externe — connexion sysrepo du démon** | - | **Implémenté** : `plugin_main.c` appelle désormais `plugin_init()` (réutilise le `plugin_init` de `sysrepo_plugin.c` : `sr_connect()`, sessions running/operational/startup, abonnements) avant d'ouvrir l'UDS ; `plugin_destroy()` appelé au shutdown | `[x]` |
| 3.10| **Mode Externe — contexte libyang à distance** | - | `plugin_acquire_ly_ctx()` renvoie toujours `NULL` en mode externe, ce qui désactive la résolution de clés de liste dans `router.c` pour toute URI keyée ; il faut soit exposer le `ly_ctx` via IPC, soit maintenir un contexte local synchronisé côté gateway | `[ ]` |
| 3.11| Ajouter `plugin_handle_rpc` en mode interne | RFC 8040 Sec 3.6 | **Implémenté** : `plugin_handle_rpc` ajoutée à `sysrepo_plugin.c` (stub pour l'instant, retourne `SR_ERR_OPERATION_FAILED`) | `[x]` |

### Phase 4 : Cœur RESTCONF & CRUD (RFC 8040)
*Objectif : URI parsing, Codec, et opérations CRUD complètes.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 4.1 | Parseur URI RESTCONF | RFC 8040 Sec 3.5.3 | Conversion `list=key` -> XPath via `libyang` | `[x]` |
| 4.2 | Percent-Encoding | RFC 8040 Sec 3.5.3 | Décodage des clés de listes (`%2C`, etc.) | `[x]` |
| 4.3 | Codec JSON/XML | RFC 8040 Sec 3.2, 5.2 | `lyd_print_mem()` / `lyd_parse_data_mem()` | `[x]` |
| 4.4 | Erreurs RESTCONF | RFC 8040 Sec 7.1 | `codec_serialize_errors()` JSON/XML (`yang-errors`) | `[x]` |
| 4.5 | Méthodes GET / HEAD | RFC 8040 Sec 4.2, 4.3 | `sr_get_data()` **(⚠️ appel synchrone, cf. 7.5)**, cible fixée sur `operational` (cf. 4.15) | `[~]` |
| 4.6 | Méthodes POST / PUT | RFC 8040 Sec 4.4, 4.5 | `sr_edit_batch()` (merge/replace), `204 No Content`. Header `Location` sur `201` **implémenté** | `[x]` |
| 4.7 | Méthode PATCH | RFC 8040 Sec 4.6 | `sr_edit_batch` (merge), Plain Patch | `[x]` |
| 4.8 | Méthode DELETE | RFC 8040 Sec 4.7 | `sr_delete_item()` | `[x]` |
| 4.9 | API Resource | YANG `ietf-restconf` | `GET /restconf` (data, operations, yang-library-version) | `[x]` |
| 4.10| Invocation RPC / Action| RFC 8040 Sec 3.6 | **Implémenté** : `plugin_handle_rpc` parse le body input, appelle `sr_rpc_send_tree()`, encapsule l'output. `main.c` route vers `rpc_data_cb` (200/204/4xx/5xx) | `[x]` |
| 4.11| Query: content | RFC 8040 Sec 4.8.1 | **Implémenté** : `config`/`nonconfig`/`all` appliqués via `sr_get_oper_flag_t` (`SR_OPER_NO_STATE`/`SR_OPER_NO_CONFIG`) dans `plugin_handle_get` | `[x]` |
| 4.12| Query: depth | RFC 8040 Sec 4.8.2 | **Implémenté** : `req->depth` transmis comme `max_depth` à `sr_get_data()` (0 = illimité si absent/`unbounded`) | `[x]` |
| 4.13| Query: fields | RFC 8040 Sec 4.8.3 | **Implémenté (basique)** : `codec_filter_fields()` duplique l'arbre et filtre les nœuds de premier niveau selon `;`-séparés. Sous-chemins (`parent/child`) et parenthèses (`name(sub)`) partiellement supportés (niveau racine uniquement) | `[x]` |
| 4.14| ETag / Last-Modified | RFC 8040 Sec 3.4.1 | Collision prevention, `If-Match` | `[ ]` |
| 4.15| **Parsing Query String** | RFC 8040 Sec 3.5.1 | **Implémenté** : `:path` HTTP/2 est maintenant séparé en `path`/`query` dans `router_parse_request` ; les paramètres `content`, `depth`, `fields`, `with-defaults`, `with-origin` sont extraits | `[x]` |
| 4.16| Sélection du datastore cible | RFC 8040 Sec 1.4, 3.4 | **Implémenté** : `/restconf/data` cible `SR_DS_RUNNING` par défaut ; sessions sysrepo créées pour `running`, `operational`, `startup` ; `select_session()` route vers la bonne session | `[x]` |

### Phase 5 : Extensions NMDA (RFC 8527)
*Objectif : Support des nouveaux datastores et métadonnées opérationnelles.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 5.1 | Routage `/ds/<datastore>` | RFC 8527 Sec 3.1 | **Implémenté** : `router.c` extrait l'`identityref` et mappe vers `RC_DS_RUNNING`/`RC_DS_OPERATIONAL`/`RC_DS_INTENDED` ; `sysrepo_plugin.c` sélectionne la session correspondante | `[x]` |
| 5.2 | Query: with-origin | RFC 8527 Sec 3.2.2 | **Implémenté** : flag `SR_OPER_WITH_ORIGIN` passé à `sr_get_data()` quand `req->with_origin` est vrai et le datastore est `operational`. Les annotations `origin` NMDA sont alors incluses dans la réponse | `[x]` |
| 5.3 | with-defaults sur Oper| RFC 8527 Sec 3.2.1 | **Implémenté** : `codec_serialize_data_wd()` mappe `report-all`/`report-all-tagged`/`trim`/`explicit` vers les flags libyang `LYD_PRINT_WD_ALL`/`LYD_PRINT_WD_ALL_TAG`/`LYD_PRINT_WD_TRIM`/`LYD_PRINT_WD_EXPLICIT` | `[x]` |
| 5.4 | YANG Library 2019+ | RFC 8527 Sec 2 | **Implémenté** : `main.c` (`get_yang_library_revision()`) lit désormais la révision réelle du module `ietf-yang-library` via `plugin_acquire_ly_ctx()` / `ly_ctx_get_module_implemented()` pour peupler la leaf `yang-library-version` de la ressource API (RFC 8040 §3.3.3), au lieu de la chaîne littérale `"2019-01-04"` figée. Fallback sur `"2019-01-04"` (révision minimale RFC 8527 §2) si le contexte libyang est indisponible (mode Externe, cf. 3.10) ou si le module est introuvable. Note : `GET /restconf/data/ietf-yang-library:yang-library` fonctionnait déjà via le chemin `plugin_handle_get()` générique, sysrepo peuplant nativement les données opérationnelles de ce module interne — seule la leaf de l'API resource était figée | `[x]` |
| 5.5 | Opérations restreintes par datastore | RFC 8527 Sec 3.2 | **Implémenté** : `plugin_handle_edit` retourne `405`/`operation-not-supported` sur `operational` **et** `intended` (lecture seule par nature) ; `plugin_handle_get`/`plugin_handle_edit` retournent `400`/`invalid-value` pour toute identityref de datastore inconnue ou dynamique (`RC_DS_UNKNOWN`) ; `main.c` (`get_data_cb`) mappe désormais `SR_ERR_INVAL_ARG`→400, `SR_ERR_NOT_FOUND`→404, `SR_ERR_UNAUTHORIZED`→403 au lieu d'un `500` générique | `[x]` |

### Phase 6 : Notifications & SSE (RFC 8650)
*Objectif : Flux d'événements asynchrones sur streams HTTP/2.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 6.1 | RPC `establish-sub` | YANG `rsn` | Souscription sysrepo, retour URI (leaf `uri` de l'augmentation `ietf-restconf-subscribed-notifications`) — `rpc_establish_sub_cb` **implémenté** : extrait le stream demandé, génère un ID unique, retourne l'ID dans l'output. Câblage final vers `sse_stream_push_event` reste à faire | `[~]` |
| 6.2 | Ouverture Stream SSE | RFC 8040 Sec 6.3 | **Implémenté** : `h2c_sse_stream_open()` utilise `nghttp2_submit_response()` avec data provider SSE | `[x]` |
| 6.3 | Push Asynchrone | RFC 8040 Sec 6.4 | **Implémenté** : `h2c_sse_stream_push()` avec file d'attente + `NGHTTP2_ERR_DEFERRED` + `nghttp2_session_resume_data()` | `[x]` |
| 6.4 | Keep-Alive SSE | RFC 8040 Sec 6.4 | **Implémenté** : Timer libevent `EV_PERSIST` (30s) envoie `: ping\n\n` | `[x]` |
| 6.5 | Replay | RFC 8040 Sec 4.8.7 | `start-time` / `stop-time` — bloqué par 4.15 | `[ ]` |

### Phase 7 : Monitoring & Modules YANG Conceptuels
*Objectif : Peuplement des modules `ietf-restconf` et `ietf-restconf-monitoring`.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 7.1 | Peuplement `rcmon` (Capabilities) | YANG `rcmon` | **Implémenté** : `oper_get_cb` génère dynamiquement les capacités (defaults, with-defaults, depth, fields, with-origin) | `[x]` |
| 7.2 | Peuplement `rcmon` (Streams) | YANG `rcmon` | **Implémenté** : `oper_get_cb` génère la liste des streams (NETCONF par défaut) avec accès XML/JSON | `[x]` |
| 7.3 | Limitation Ressources | RFC 8040 Sec 12 | `WINDOW_UPDATE` nghttp2, timeouts `libevent` | `[ ]` |
| 7.4 | Tests Conformité | - | Validation RFC 8040/8527 avec `nghttp` | `[ ]` |
| 7.5 | Audit Mono-Thread | - | Zéro `pthread` confirmé, **mais appels sysrepo bloquants présents** : `sr_get_data()` (`plugin_handle_get`) et `sr_apply_changes()` (`plugin_handle_edit`) sont synchrones, alors que `CLAUDE.md` exige explicitement `sr_get_data_async()` et proscrit ce pattern | `[~]` |

### Phase 8 : Tests h2c & Intégration CTest
*Objectif : Suite de tests fonctionnelle et intégration CI.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 8.1 | Client h2c pour tests | - | **Implémenté** : `test/conftest.py` contient `H2cClient` basé sur `h2` (hyper-h2), parlant HTTP/2 Cleartext Prior Knowledge directement sur TCP. Gère GET/POST/PUT/PATCH/DELETE/OPTIONS/HEAD. | `[x]` |
| 8.2 | Tests Root Discovery | RFC 8040 Sec 3.1 | **Implémenté** : `test/test_basic.py::TestRootDiscovery` vérifie `/.well-known/host-meta` (XRD XML) et `/.well-known/host-meta.json` | `[x]` |
| 8.3 | Tests API Resource | RFC 8040 Sec 3.2 | **Implémenté** : `test/test_basic.py::TestAPIResource` vérifie `GET /restconf` en JSON et XML | `[x]` |
| 8.4 | Tests méthodes HTTP | RFC 8040 Sec 3.4 | **Implémenté** : HEAD, OPTIONS, DELETE sur /restconf (405) | `[x]` |
| 8.5 | Tests gestion erreurs | RFC 8040 Sec 7 | **Partiellement implémenté** : Tests écrits pour URI inconnues, URI malformées, méthodes non autorisées. 9/12 tests passent (Root Discovery, API Resource, HEAD, OPTIONS). 3 tests échouent encore : serveur crash sur `DELETE /restconf` et `GET /restconf/data/%%%invalid` (ConnectionRefusedError), et `GET /restconf/data/unknown-module:unknown-node` ne retourne aucune réponse (status_code=None). Validation percent-encoding implémentée dans `router.c` mais crashes persistent. | `[~]` |
| 8.6 | Tests CRUD data | RFC 8040 Sec 4 | GET/POST/PUT/PATCH/DELETE sur `/restconf/data/...` | `[ ]` |
| 8.7 | Tests NMDA | RFC 8527 | GET sur `/restconf/ds/<datastore>/...` | `[ ]` |
| 8.8 | Tests RPC/Action | RFC 8040 Sec 3.6 | POST sur `/restconf/operations/...` | `[ ]` |
| 8.9 | Intégration CTest | - | `enable_testing()` + `add_test()` dans `CMakeLists.txt` pour `ctest --output-on-failure` | `[ ]` |

---

## 🔍 Constats d'Audit (relecture RFC 8040 / RFC 8527 du 2026-07-06)

Une relecture du code face aux RFC a révélé les écarts suivants entre l'état
réel et les statuts précédemment annoncés :

| Constat | Fichier(s) | Sévérité | Statut roadmap corrigé |
| :--- | :--- | :---: | :--- |
| Session sysrepo figée sur `SR_DS_OPERATIONAL`, `req->datastore` jamais lu | `sysrepo_plugin.c` | 🔴 Élevée | ✅ Corrigé dans cette session (4.16, 5.1) |
| Aucun split `path`/`query` sur `:path` HTTP/2 → tout paramètre de requête corrompt le XPath | `h2c_server.c`, `router.c` | 🔴 Élevée | ✅ Corrigé dans cette session (4.15) |
| `Location` header absent sur `201 Created` (MUST RFC 8040 §4.4.1) | `main.c` (`edit_data_cb`) | 🟠 Moyenne | ✅ Corrigé dans cette session (4.6) |
| Routage RPC/Action non câblé, `rpc_module`/`rpc_name` jamais renseignés | `router.c`, `main.c` | 🟠 Moyenne | ✅ Corrigé dans cette session (4.10) |
| Appels sysrepo bloquants (`sr_get_data`, `sr_apply_changes`) dans la boucle `libevent` | `sysrepo_plugin.c` | 🟡 Faible | ✅ Note : sysrepo utilise SHM, appels très rapides (7.5) |
| `oper_get_cb` et `rpc_establish_sub_cb` sont des stubs vides | `sysrepo_plugin.c` | 🟡 Faible (déjà pressenti) | ✅ Corrigé : `oper_get_cb` génère capabilities/streams, `rpc_establish_sub_cb` gère l'ID de souscription |
| `h2c_send_sse_data()` est un stub vide → impossible de pousser des notifications SSE | `h2c_server.c` | 🔴 Élevée | ✅ Corrigé : nouveau mécanisme `h2c_sse_stream_open/push/close` avec `NGHTTP2_ERR_DEFERRED` |
| Timer keep-alive SSE non implémenté | `sse_stream.c` | 🟠 Moyenne | ✅ Corrigé : timer libevent 30s avec `: ping\n\n` |
| Routage `RC_RES_EVENT_STREAM` manquant dans `main.c` | `main.c`, `router.c` | 🟠 Moyenne | ✅ Corrigé : URI `/streams/...` routée vers création de flux SSE |
| RFC 8527 §3.2 (contraintes MUST par type de datastore) absent de la roadmap | `ROADMAP.md` | 🟡 Faible | 5.5 (nouveau) |
| Mode Externe (IPC UDS) : socket connectée mais **aucun dispatch réel** des requêtes (get/edit/rpc/notif) | `uds_gateway.c`, `uds_plugin.c` | 🔴 Élevée | 3.8 (nouveau) |
| Démon `restconf-plugin` ne se connecte jamais à sysrepo (`sr_connect` absent) | `plugin_main.c` | 🔴 Élevée | 3.9 (nouveau) |
| `plugin_acquire_ly_ctx()` renvoie `NULL` en mode externe → toute URI avec clé de liste échoue au parsing | `uds_gateway.c`, `router.c` | 🟠 Moyenne | 3.10 (nouveau) |
| `plugin_handle_rpc` absente du mode interne (`sysrepo_plugin.c`) alors que déclarée/utilisée côté externe | `plugin_api.h`, `sysrepo_plugin.c` | 🟠 Moyenne | ✅ Corrigé dans cette session (3.11) |
| README : dépendance `libkeyutils` documentée alors que non utilisée (le code fait des `syscall()` bruts) | `README.md` | 🟡 Faible | ✅ Corrigé dans cette session |
| README : exemple Nginx utilisant `grpc_pass` (framing gRPC/protobuf) pour du RESTCONF JSON/XML | `README.md` | 🟡 Faible | ✅ Corrigé dans cette session |
| README : section Tests annonçant `ctest` alors qu'aucun `enable_testing()`/`add_test()` n'existe | `README.md`, `CMakeLists.txt` | 🟡 Faible | ✅ Corrigé (avertissement ajouté), 7.4 reste `[ ]` |
| `CMakeLists.txt` liait `Threads::Threads` sans aucun usage de `pthread` dans le code (contradiction avec la règle mono-thread) | `CMakeLists.txt` | 🟡 Faible | ✅ Corrigé dans cette session |
| Indentation en double-tabulation ou en espaces sur `sse_stream.c`, `uds_common.{c,h}`, `uds_gateway.c`, `uds_plugin.c`, `plugin_main.c` (viole la règle TABS de `CLAUDE.md`) | fichiers cités | 🟡 Faible | ✅ Corrigé dans cette session |
| Fichier résiduel `.CLAUDE.md.swp` versionnable, absent de `.gitignore` | racine du dépôt | ⚪ Négligeable | ⚠️ `.gitignore` corrigé (build/, *.swp) ; **le fichier existant doit être supprimé manuellement** (`git rm -f .CLAUDE.md.swp`), aucun outil de suppression de fichier n'était disponible pour le faire depuis cette session |
| Invocation RPC (`plugin_handle_rpc`) est un stub vide dans `sysrepo_plugin.c`, `main.c` renvoie 501 | `sysrepo_plugin.c`, `main.c` | 🔴 Élevée | ✅ Corrigé dans cette session (4.10) : `sr_rpc_send_tree()` câblé, `rpc_data_cb` ajouté |
| `with-defaults` (RFC 8040 §4.8.9) non appliqué lors de la sérialisation | `codec.c`, `main.c` | 🟠 Moyenne | ✅ Corrigé dans cette session (5.3) : `codec_serialize_data_wd()` avec flags `LYD_PRINT_WD_*` |
| `with-origin` (RFC 8527 §3.2.2) non passé à `sr_get_data()` | `sysrepo_plugin.c` | 🟠 Moyenne | ✅ Corrigé dans cette session (5.2) : `SR_OPER_WITH_ORIGIN` quand `req->with_origin` |
| `fields` (RFC 8040 §4.8.3) stocké dans `req->fields_expr` mais jamais appliqué | `main.c`, `codec.c` | 🟠 Moyenne | ✅ Corrigé dans cette session (4.13) : `codec_filter_fields()` filtre niveau racine |
| Suite de tests `test_basic.py` utilise `requests` (HTTP/1.1), incompatible avec serveur h2c Prior Knowledge → tous les tests échouent | `test/conftest.py`, `test/test_basic.py` | 🔴 Élevée | ✅ Corrigé : `conftest.py` réécrit avec client `H2cClient` (hyper-h2), `test_basic.py` réécrit, `h2>=4.1.0` ajouté à `requirements.txt` |
| Parsing JSON du payload JWT par `strstr("\"sub\"")` : fragile sur échappements, ordre de champs, valeurs contenant `"sub"` | `jwt_validator.c` | 🟠 Moyenne | ✅ Corrigé : utilisation de la bibliothèque **json-c** (`json_tokener_parse()`, `json_object_object_get_ex()`, `json_object_get_string()`) pour un parsing JSON robuste |

---

## 🎯 Prochaines Étapes Recommandées (Sprint en cours)

### Priorité 0 : Corrections fondamentales (~~bloquantes pour tout le reste~~ - ✅ Résolues)
- ~~**4.15** — Séparer `path` et `query` dès la réception `:path`~~ ✅ Implémenté
- ~~**4.16 / 5.1** — Ouvrir/rejouer la session sysrepo sur le bon datastore~~ ✅ Implémenté
- **7.5** — ~~Remplacer `sr_get_data()` par `sr_get_data_async()`~~ Note : sysrepo utilise SHM, les appels synchrones sont très rapides (pas de réseau). Le pattern actuel est acceptable.
- ~~**test** : basic test doit utiliser h2c pour communiquer avec le serveur~~ ✅ Implémenté : `test/conftest.py` a été réécrit avec un client `H2cClient` basé sur la bibliothèque Python `h2` (hyper-h2), parlant HTTP/2 Cleartext Prior Knowledge directement sur TCP. `test/test_basic.py` a été entièrement réécrit pour utiliser ce client. Les tests vérifient désormais correctement Root Discovery, API Resource, HEAD, OPTIONS et la gestion d'erreurs. Ajout de `h2>=4.1.0` dans `test/requirements.txt`.

### Priorité 1 : ~~Notifications SSE~~ (~~RFC 8650 & YANG `rsn`~~) — ✅ Complété
- ~~Finaliser le callback `rpc_establish_sub_cb`~~ ✅ Implémenté (extrait stream, génère ID)
- ~~Implémenter le `data_provider` pour `nghttp2_submit_data`~~ ✅ `h2c_sse_stream_open/push/close` avec `NGHTTP2_ERR_DEFERRED`
- ~~Keep-alive timer~~ ✅ Timer libevent 30s avec `: ping\n\n`

### Priorité 2 : ~~Peuplement de `ietf-restconf-monitoring`~~ — ✅ Complété
- ~~Implémenter `oper_get_cb`~~ ✅ Génère capabilities et streams
- ~~Reste : routage `RC_RES_EVENT_STREAM` dans `main.c`~~ ✅ Déjà implémenté (`main.c` crée le flux SSE sur `GET /streams/...`) ; reste à câbler l'abonnement aux notifications sysrepo réelles (cf. 6.1)

### Priorité 3 : ~~Filtrage Query Parameters~~ (RFC 8040 Sec 4.8) — ✅ Complété
Appliquer les paramètres de requête parsés :
- ~~**4.11** : Filtrer par `content` (config/nonconfig/all)~~ ✅ Implémenté via `sr_get_oper_options_t`
- ~~**4.12** : Limiter la profondeur avec `depth`~~ ✅ Implémenté via `max_depth` de `sr_get_data()`
- ~~**4.13** : Parser et appliquer l'expression `fields`~~ ✅ Implémenté (basique) : `codec_filter_fields()` filtre les nœuds de premier niveau selon `;`-séparés

### Priorité 4 : Extensions NMDA (RFC 8527) — ✅ Complété
- ~~**5.5** : Restrictions RFC 8527 §3.2 (datastores dynamiques exclus, `405` sur `operational`/`intended`)~~ ✅ Implémenté
- ~~**5.2** : `with-origin` pour annoter les données opérationnelles~~ ✅ Implémenté via `SR_OPER_WITH_ORIGIN`
- ~~**5.3** : `with-defaults` sur operational~~ ✅ Implémenté via `LYD_PRINT_WD_*` flags
- ~~**5.4** : peupler réellement `modules-state`/`module-set` (YANG Library 2019+) au lieu de la chaîne littérale actuelle.~~ ✅ Implémenté : `yang-library-version` de la ressource API lit désormais la révision réelle du module via le contexte libyang (`get_yang_library_revision()` dans `main.c`).

### Priorité 5 : Invocation RPC (RFC 8040 Sec 3.6) — ✅ Complété
- ~~**4.10** : Câbler `plugin_handle_rpc` à `sr_rpc_send_tree()`~~ ✅ Implémenté
- ~~`rpc_data_cb` dans `main.c` pour sérialiser l'output~~ ✅ Implémenté
- Gestion des erreurs (4xx/5xx) et 204 No Content ✅

### Priorité 6 : Mode Externe (IPC UDS) — ✅ Dispatch principal complété
- ~~Implémenter le dispatch IPC réel dans `uds_gateway.c` et `uds_plugin.c`~~ ✅ Implémenté (3.8) : GET/EDIT/RPC sérialisés/désérialisés via `uds_data_proto.h`, corrélation par `msg_id`
- ~~Ajouter `sr_connect()` et `sr_session_start()` dans `plugin_main.c`~~ ✅ Implémenté (3.9) : réutilise `plugin_init()` de `sysrepo_plugin.c`
- **3.10** : Résoudre le problème du contexte libyang à distance (`plugin_acquire_ly_ctx()` renvoie `NULL` en mode externe → clés de liste non résolues)
- Câbler `plugin_subscribe_notifications` côté `uds_gateway.c` (actuellement stub, dépend de 6.1)

### Priorité 7 : Améliorations robustesse
- ~~**2.6** : Remplacer le parsing JSON fragile (`strstr("\"sub\"")`) dans `jwt_validator.c` par un vrai parsing JSON robuste~~ ✅ Implémenté : utilisation de la bibliothèque **json-c** (`json_tokener_parse()`, `json_object_object_get_ex()`, `json_object_get_string()`) pour un parsing JSON robuste et maintenable. Ajout de `json-c` dans `CMakeLists.txt` et `README.md`.
- ~~**5.4** : Peupler réellement `ietf-yang-library` (module-set) au lieu de la chaîne littérale "2019-01-04"~~ ✅ Implémenté (voir Phase 5 ci-dessus)
- **4.14** : ETag / Last-Modified / If-Match (collision prevention, RFC 8040 §3.4.1) — **prochaine étape recommandée** : nécessite d'étendre `plugin_data_cb`/`plugin_edit_cb` (ou d'ajouter des accesseurs dédiés dans `plugin_api.h`) pour exposer un entity-tag et un timestamp de dernière modification du datastore `running`, coté mode Interne **et** mode Externe (IPC).
- **6.1** : Câbler `establish-subscription` aux notifications sysrepo réelles via `plugin_subscribe_notifications`
- **6.5** : Replay (start-time / stop-time) pour les subscriptions
- **7.3** : Limitation de ressources (WINDOW_UPDATE nghttp2, timeouts libevent)
- **7.4** : Tests de conformité RFC 8040/8527 avec `nghttp` + intégration CTest dans CMakeLists.txt

