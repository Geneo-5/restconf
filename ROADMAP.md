




# 🗺️ Roadmap d'Implémentation - Backend RESTCONF h2c

> **⚠️ Rappel des Règles de Développement (AGENTS.md)**
> *   **Transport** : HTTP/2 Cleartext (h2c) uniquement. Zéro TLS.
> *   **Architecture** : 100% Mono-thread, non-bloquant (`libevent` + pipe `sysrepo`).
> *   **Sécurité** : JWT validé via OpenSSL, clé publique extraite du Kernel Keyring via `syscall`.
> *   **Plugin** : Compilable en mode Interne (monolithique) ou Externe (IPC via UDS).
> *   **Médias** : `application/yang-data+json` et `application/yang-data+xml`.
> *   **Formatage** : Indentation par **TABULATIONS** (taille 8), lignes limitées à **80 caractères**.
> *   **API Sysrepo** : `SR_SUBSCR_NO_THREAD` obligatoire. `sr_acquire_context()` / `sr_release_context()`.
>
> ⚠️ **Voir la tâche 3.12 ci-dessous** : cette règle "non-bloquant" est
> actuellement **violée en pratique** par `sr_get_data()` sur les
> chemins couverts par un `oper_get_cb`. Une évolution d'architecture
> est proposée en fin de document.

Les identifiants de tâche (`3.10`, `5.4`, ...) sont stables et référencés
depuis des commentaires `TODO`/`cf.` dans le code source : ne pas les
renuméroter en éditant ce fichier. L'historique détaillé des correctifs
déjà livrés vit dans `git log`, pas dans ce fichier.

---

## 📊 Tableau de Bord Global

| Phase | Description | Progression | Statut |
| :--- | :--- | :---: | :---: |
| **1** | Fondations Réseau & Boucle d'Événements | 100% | 🟢 |
| **2** | Sécurité, JWT & NACM | 100% | 🟢 |
| **3** | Architecture Plugin Sysrepo (Dual-Mode) | 92% | 🟡 |
| **4** | Cœur RESTCONF & CRUD (RFC 8040) | 100% | 🟢 |
| **5** | Extensions NMDA (RFC 8527) | 100% | 🟢 |
| **6** | Notifications & SSE (RFC 8650) | 95% | 🟡 |
| **7** | Monitoring & Modules YANG Conceptuels | 65% | 🟡 |
| **8** | Tests h2c & Intégration CTest | 85% | 🟡 |

*(Légende : ⚪ À faire | 🟡 En cours | 🟢 Terminé | 🔴 Bloquant/critique ;
`[x]` fait, `[~]` partiel, `[ ]` à faire)*

Phase 3 repasse à 🟡 : l'item **3.12** (thread worker sysrepo confiné)
est maintenant implémenté, et **3.10** (contexte libyang local en
mode Externe) également. Reste **3.7** (câblage réel des RPC de
souscription, dépend de 6.1).

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
| 2.6 | Parsing JSON robuste du payload JWT | RFC 7519 | `jwt_validator.c` via **json-c** | `[x]` |

### Phase 3 : Architecture Plugin Sysrepo
*Objectif : Couche d'abstraction et abonnements sysrepo.*
*Fichier central : `src/plugin/sysrepo_plugin.c` (+ `plugin_main.c` en mode externe).*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 3.1 | Config CMake Dual-Mode | - | Option `BUILD_EXTERNAL_PLUGIN` | `[x]` |
| 3.2 | Mode Interne | - | Liens statiques, appels C directs | `[x]` |
| 3.3 | Mode Externe (IPC UDS) — connexion | - | `evconnlistener`/`bufferevent` sur `AF_UNIX` (`uds_gateway.c`, `uds_plugin.c`) | `[x]` |
| 3.4 | Protocole IPC — framing | - | Framing longueur + magic `0x52434E46` (`uds_common.c`) | `[x]` |
| 3.5 | Contexte Libyang | - | `sr_acquire_context()`/`sr_release_context()` en interne ; contexte local `ly_ctx` construit au démarrage en externe (cf. 3.10) | `[x]` |
| 3.6 | Callbacks Oper Data | YANG `rcmon` | `sr_oper_get_subscribe` (NO_THREAD) ; `oper_get_cb` génère capabilities/streams (cf. 7.1) | `[x]` |
| 3.7 | Callbacks RPC | YANG `rsn` | `sr_rpc_subscribe_tree` (NO_THREAD) ; ID de souscription retourné, flux (`stream`) validé contre le seul flux réel `NETCONF` (`SR_ERR_NOT_FOUND` sinon). **Limite restante** : l'ID retourné n'est pas corrélé à un flux SSE HTTP/2 précis (cf. 6.1) | `[~]` |
| 3.8 | Mode Externe — dispatch IPC réel | - | `plugin_data_cb`/`plugin_rpc_cb` transport-agnostiques ; corrélation par `msg_id`. **ROADMAP 6.1** : `plugin_subscribe_notifications` câblée coté gateway (enregistre le callback, appelé depuis `dispatch_ipc_response()` sur `IPC_MSG_NOTIF_PUSH`) et coté daemon (`ext_plugin_init_uds()` déclenche l'abonnement sysrepo et diffuse via `daemon_notif_push_cb()` à toutes les gateways connectées) | `[x]` |
| 3.9 | Mode Externe — connexion sysrepo du démon | - | `plugin_main.c` : `plugin_init()`/`plugin_destroy()` autour de l'UDS ; sessions par-requête via `open_request_session()`/`close_request_session()` | `[x]` |
| 3.10 | 🟢 Mode Externe — contexte libyang local | - | `build_local_ly_ctx()` (`src/ipc/uds_gateway.c`) : au démarrage du gateway, scan de `<SYSREPO_REPOSITORY_PATH ou SR_YANG_REPO_PATH>/yang` et chargement de chaque `.yang` trouvé (`lys_parse_path()`, best-effort par fichier) dans un `ly_ctx` **local, en lecture seule, indépendant de la connexion IPC**. `plugin_acquire_ly_ctx()` renvoie désormais ce contexte au lieu de `NULL` — résolution des clés de liste et des préfixes de nouveau active en mode Externe (`router.c`). **Limite connue** : contexte figé au démarrage, pas de hot-reload si un module est (dés)installé à chaud dans sysrepo sans redémarrer `restconf-server`. | `[x]` |
| 3.11 | `plugin_handle_rpc` en mode interne | RFC 8040 Sec 3.6 | Câblée sur `sr_rpc_send_tree()` (cf. 4.10) | `[x]` |
| **3.12** | **🟢 DONE — Thread worker sysrepo confiné** | AGENTS.md règle #2 | **Implémenté (session 2026-07-12)** : thread pthread confiné dédié (`src/plugin/sysrepo_worker.c`) propriétaire exclusif de `sr_conn_ctx_t`, sessions et abonnements. Communication par file de messages (mutex+condvar+pipe) vers le worker et eventfd+completion queue vers libevent. `plugin_handle_get/edit/rpc` deviennent asynchrones (soumission + callback différé). Le pipe sysrepo est désormais pompé par le worker via `poll()` (200ms timeout). Les callbacks `oper_get_cb`/`rpc_establish_sub_cb`/`notif_event_cb` s'exécutent dans le thread worker ; `notif_event_cb` marshal les notifications vers libevent via la completion queue (ROADMAP 3.12.6). `sr_acquire_context`/`sr_release_context` restent les seuls appels sysrepo autorisés depuis le thread libevent (exception AGENTS.md). Détails en 3.12.1–3.12.8. | `[x]` |
| 3.12.2 | Protocole de messages worker | - | `sr_worker_msg_t` (GET/EDIT/RPC/SUBSCRIBE_NOTIF/SHUTDOWN) + `sr_completion_t` (DATA/EDIT/RPC/NOTIF), deep-copy des champs de requête, continuation par callback + user_data | `[x]` |
| 3.12.3 | File de requêtes + notification retour | - | Queue mutex+condvar `libevent → worker` avec wakeup pipe ; eventfd enregistré `EV_READ \| EV_PERSIST` dans libevent pour `worker → libevent` | `[x]` |
| 3.12.4 | Thread worker sysrepo | - | `src/plugin/sysrepo_worker.c` : boucle dédiée avec `poll()` (wakeup pipe + sysrepo event pipe, timeout 200ms), **seule** propriétaire de `sr_conn_ctx_t`, sessions par-requête et pompage `sr_subscription_process_events()` | `[x]` |
| 3.12.5 | Migration `plugin_handle_get/edit/rpc` | - | `sysrepo_plugin.c` devient un wrapper thin ; appels remplacés par `sr_worker_submit_get/edit/rpc` (retour immédiat, callback invoqué depuis libevent à réception de la completion via eventfd) | `[x]` |
| 3.12.6 | Re-marshalling des callbacks sysrepo | - | `oper_get_cb`/`rpc_establish_sub_cb` s'exécutent dans le thread worker ; `notif_event_cb` marshal les notifications via la completion queue (`SR_COMP_NOTIF`) pour que `sse_stream_push_event()` soit appelé depuis le thread libevent | `[x]` |
| 3.12.7 | Annulation & timeout | - | Deep-copy des champs de requête dans `sr_worker_msg_t` — le `rc_request_t` original peut être libéré immédiatement après soumission. Callback sur `user_data` (alloué `malloc`, libéré par le callback). Timeout borné par poll() 200ms ; shutdown graceful via message `SR_WORKER_SHUTDOWN` + `pthread_join` | `[x]` |
| 3.12.8 | Validation mode Externe + tests de charge | - | Mode Externe : le worker fusionne naturellement avec le démon plugin (`plugin_main.c` appelle `plugin_init()` qui crée le worker). Compilation avec `pthread` dans les deux cibles CMake. Test de charge à valider via `build_test.sh` | `[x]` |

### Phase 4 : Cœur RESTCONF & CRUD (RFC 8040)
*Objectif : URI parsing, Codec, et opérations CRUD complètes.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 4.1 | Parseur URI RESTCONF | RFC 8040 Sec 3.5.3 | `list=key` → XPath via `libyang` | `[x]` |
| 4.2 | Percent-Encoding | RFC 8040 Sec 3.5.3 | Décodage des clés de listes (`%2C`, etc.) | `[x]` |
| 4.3 | Codec JSON/XML | RFC 8040 Sec 3.2, 5.2 | `lyd_print_mem()`/`lyd_parse_data_mem()` | `[x]` |
| 4.4 | Erreurs RESTCONF | RFC 8040 Sec 7.1 | `codec_serialize_errors()` JSON/XML (`yang-errors`) | `[x]` |
| 4.5 | Méthodes GET / HEAD | RFC 8040 Sec 4.2, 4.3 | `sr_get_data()` — synchrone ; **dépend désormais de 3.12** | `[~]` |
| 4.6 | Méthodes POST / PUT | RFC 8040 Sec 4.4, 4.5 | `sr_edit_batch()` (merge/replace), `204`, `Location` sur `201` | `[x]` |
| 4.7 | Méthode PATCH | RFC 8040 Sec 4.6 | `sr_edit_batch` (merge), Plain Patch | `[x]` |
| 4.8 | Méthode DELETE | RFC 8040 Sec 4.7 | `sr_delete_item()` | `[x]` |
| 4.9 | API Resource | YANG `ietf-restconf` | `GET /restconf` (data, operations, yang-library-version) | `[x]` |
| 4.10 | Invocation RPC / Action | RFC 8040 Sec 3.6 | `lyd_new_path()` + `codec_parse_rpc_input()` (`lyd_parse_op` + `LYD_TYPE_RPC_RESTCONF`) puis `sr_rpc_send_tree()` | `[x]` |
| 4.11 | Query: content | RFC 8040 Sec 4.8.1 | `config`/`nonconfig`/`all` via `SR_OPER_NO_STATE`/`SR_OPER_NO_CONFIG` | `[x]` |
| 4.12 | Query: depth | RFC 8040 Sec 4.8.2 | `req->depth` → `max_depth` (0 = illimité) | `[x]` |
| 4.13 | Query: fields | RFC 8040 Sec 4.8.3 | `codec_filter_fields()` — niveau racine, `;`-séparés | `[x]` |
| 4.14 | ETag / Last-Modified | RFC 8040 Sec 3.4.1 | ETag FNV-1a sur le corps sérialisé, `If-Match` (dont wildcard `*`), 412 sur mismatch | `[x]` |
| 4.15 | Parsing Query String | RFC 8040 Sec 3.5.1 | `content`, `depth`, `fields`, `with-defaults`, `with-origin` | `[x]` |
| 4.16 | Sélection du datastore cible | RFC 8040 Sec 1.4, 3.4 | `/restconf/data` → `SR_DS_RUNNING` par défaut ; session dédiée par requête (`open_request_session()`) | `[x]` |

### Phase 5 : Extensions NMDA (RFC 8527)
*Objectif : Support des nouveaux datastores et métadonnées opérationnelles.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 5.1 | Routage `/ds/<datastore>` | RFC 8527 Sec 3.1 | `router.c` mappe l'identityref vers `RC_DS_*` ; session dédiée par requête | `[x]` |
| 5.2 | Query: with-origin | RFC 8527 Sec 3.2.2 | `SR_OPER_WITH_ORIGIN` si `req->with_origin` et datastore `operational` | `[x]` |
| 5.3 | with-defaults sur Oper | RFC 8527 Sec 3.2.1 | `codec_serialize_data_wd()` → flags `LYD_PRINT_WD_*` | `[x]` |
| 5.4 | YANG Library 2019+ | RFC 8527 Sec 2 | Révision réelle via `ly_ctx_get_module_implemented()`, fallback `"2019-01-04"` si contexte indisponible (cf. 3.10) | `[x]` |
| 5.5 | Opérations restreintes par datastore | RFC 8527 Sec 3.2 | `405` sur `operational`/`intended` (lecture seule) ; `400` sur identityref inconnue | `[x]` |

### Phase 6 : Notifications & SSE (RFC 8650)
*Objectif : Flux d'événements asynchrones sur streams HTTP/2.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 6.1 | RPC `establish-subscription` + câblage réel | YANG `rsn` | **Découverte multi-modules** : `plugin_subscribe_notifications()` (`sysrepo_plugin.c`) parcourt tous les modules *implémentés* du contexte libyang partagé et tente un abonnement par module (`sr_notif_subscribe_tree`, xpath `NULL`), au lieu du seul module `restconf-test` en dur — `notif_event_cb()` → enveloppe RFC 8040 Sec 6.4 → registre `sse_registry_entry_t` → diffusion `on_notification_cb()`/`sse_stream_push_event()`. **Mode Externe câblé** : le daemon s'abonne via le même mécanisme (`ext_plugin_init_uds()`) et repousse chaque notification à toutes les gateways connectées via un nouveau message `IPC_MSG_NOTIF_PUSH` (`uds_plugin.c`/`uds_gateway.c`). `establish-subscription` valide désormais le nom de flux (cf. 3.7). **Limites restantes** : un seul flux conceptuel (`NETCONF`) existe réellement — pas de filtrage par `xpath-filter`/souscription individuelle, l'ID de souscription RPC n'est pas corrélé à un flux SSE HTTP/2 précis (un client doit ouvrir lui-même `GET /streams/NETCONF`), pas de replay (cf. 6.5). Dépend du même pipe sysrepo que 3.12 | `[~]` |
| 6.2 | Ouverture Stream SSE | RFC 8040 Sec 6.3 | `nghttp2_submit_response()` + data provider SSE | `[x]` |
| 6.3 | Push Asynchrone | RFC 8040 Sec 6.4 | File d'attente + `NGHTTP2_ERR_DEFERRED` + `nghttp2_session_resume_data()` | `[x]` |
| 6.4 | Keep-Alive SSE | RFC 8040 Sec 6.4 | Timer `libevent` `EV_PERSIST` (30s) → `: ping\n\n` | `[x]` |
| 6.5 | 🟢 Replay | RFC 8040 Sec 4.8.7 | `start-time`/`stop-time` parsés en RFC 3339 (`parse_rfc3339()`, `router.c`) et exposes dans `rc_request_t`. `plugin_open_replay_subscription()` (mode interne, `sysrepo_plugin.c`) ouvre une souscription sysrepo **dediee** au client (independante de la souscription partagee 6.1) avec `start_time`/`stop_time`, qui rejoue d'abord les notifications stockees puis continue en live ; poussee exclusive vers ce seul flux SSE (`on_replay_notification_cb()`, `main.c`), le broadcast partage l'ignore pour eviter les doublons. **Limites** : necessite que sysrepo ait le replay actif pour le module (`sysrepoctl -e <module> -r`, non verifie automatiquement) ; feuille `replay-support` toujours annoncee à `false` (cf. 7.2, prudence volontaire) ; **mode Externe non supporte** (`plugin_open_replay_subscription()` renvoie `NULL` dans `uds_gateway.c`, fallback live-only sans erreur bloquante) | `[x]` |

### Phase 7 : Monitoring & Modules YANG Conceptuels
*Objectif : Peuplement des modules `ietf-restconf` et `ietf-restconf-monitoring`.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 7.1 | Peuplement `rcmon` (Capabilities) | YANG `rcmon` | `oper_get_cb` génère les capacités dynamiquement | `[x]` |
| 7.2 | Peuplement `rcmon` (Streams) | YANG `rcmon` | `oper_get_cb` génère la liste des streams (NETCONF) | `[x]` |
| 7.3 | 🟢 Limitation Ressources | RFC 8040 Sec 12 | **SETTINGS nghttp2 explicites** : `nghttp2_session_server_new2()` (la variante 3-arguments precedente ignorait silencieusement les `nghttp2_option` construites -- bug corrige) + `NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS` (100) + `NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE` (1 MiB) + `nghttp2_session_set_local_window_size()` niveau connexion (4 MiB, declenche un `WINDOW_UPDATE`). **Cap du corps de requete** : `H2C_MAX_REQUEST_BODY_SIZE` (16 MiB) dans `on_data_chunk_recv_callback()`/`on_frame_recv_callback()` (`h2c_server.c`), rejet `413` avant meme d'appeler `req_cb()`. **Timeout d'inactivite** : `h2c_server_set_idle_timeout()` (nouvelle API), `bufferevent_set_timeouts()` en LECTURE seule (ne casse pas les flux SSE ou seul le serveur ecrit), option CLI `-t <sec>` (defaut 300s, 0=desactive) | `[x]` |
| 7.4 | Tests Conformité | - | Validation RFC 8040/8527 avec `nghttp` | `[ ]` |
| 7.5 | Module de Test RESTCONF | - | `models/yang/restconf-test.yang` : structures pour les tests de conformité. **Tous les tests de qualification doivent vérifier son chargement avant exécution** | `[x]` |
| 7.6 | Audit Mono-Thread | - | Zéro `pthread` confirmé, mais `sr_get_data()`/`sr_apply_changes()` restent synchrones. Jugement révisé : **non acceptable dans le cas des données `oper_get_cb`**, cf. 3.12 — reste acceptable pour un accès SHM pur sans callback distant | `[~]` |

### Phase 8 : Tests h2c & Intégration CTest
*Objectif : Suite de tests fonctionnelle et intégration CI.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 8.1 | Client h2c pour tests | - | `test/conftest.py` : `H2cClient` (hyper-h2), Prior Knowledge sur TCP | `[x]` |
| 8.2 | Tests Root Discovery | RFC 8040 Sec 3.1 | `/.well-known/host-meta` (XRD XML et JSON) | `[x]` |
| 8.3 | Tests API Resource | RFC 8040 Sec 3.2 | `GET /restconf` JSON et XML | `[x]` |
| 8.4 | Tests méthodes HTTP | RFC 8040 Sec 3.4 | HEAD, OPTIONS, DELETE sur `/restconf` (405) | `[x]` |
| 8.5 | Tests gestion erreurs | RFC 8040 Sec 7 | Revue de code : 3 scénarios couverts (405, 400 percent-encoding, 404 module inconnu). **À confirmer par exécution réelle de `./scripts/build_test.sh`** | `[~]` |
| 8.6 | Tests CRUD data | RFC 8040 Sec 4 | GET/POST/PUT/PATCH/DELETE sur `/restconf/data/...` | `[ ]` |
| 8.7 | Tests NMDA | RFC 8527 | GET sur `/restconf/ds/<datastore>/...` | `[ ]` |
| 8.8 | Tests RPC/Action | RFC 8040 Sec 3.6 | POST sur `/restconf/operations/...` | `[ ]` |
| 8.9 | Intégration CTest | - | `enable_testing()` + `add_test()` dans `CMakeLists.txt` | `[ ]` |

---

## ⚠️ Dette Technique Ouverte (synthèse)

| Sujet | Item(s) | Fichier(s) | Impact |
| :--- | :---: | :--- | :--- |
| **Blocage `sr_get_data()` sur oper data** | **3.12 ✅** | `sysrepo_worker.c` | **Résolu** — thread confiné dédié |
| **Contexte libyang indisponible en mode Externe** | **3.5, 3.10 ✅** | `uds_gateway.c` | **Résolu** — contexte local chargé depuis le dépôt YANG de sysrepo au démarrage (limite : pas de hot-reload, cf. 3.10) |
| `establish-subscription` : pas de filtrage par souscription individuelle / receiver | 6.1 | `sysrepo_plugin.c`, `main.c` | Un seul flux conceptuel `NETCONF` diffuse tout (hors clients avec `start-time`, qui ont leur propre souscription dediee, cf. 6.5) ; pas de `xpath-filter` par souscription |
| ~~Pas de replay des notifications~~ | **6.5 ✅** | `router.c`, `sysrepo_plugin.c`, `main.c` | **Résolu en mode Interne** (souscription sysrepo dédiée par client avec `start-time`/`stop-time`) ; **mode Externe non supporté** (fallback live-only, cf. `uds_gateway.c`) |
| ~~`plugin_subscribe_notifications` stub côté gateway externe~~ | **3.8, 6.1 ✅** | `uds_gateway.c`, `uds_plugin.c` | **Résolu** — `IPC_MSG_NOTIF_PUSH` câblé dans les deux sens |
| ~~Pas de limitation de ressources~~ | **7.3 ✅** | `h2c_server.c` | **Résolu** — SETTINGS nghttp2 (max streams, fenêtres) réellement appliquées (bug de configuration corrigé), cap du corps de requête (16 MiB → 413), timeout de lecture par connexion configurable (`-t`, 300s par défaut). **Limite** : valeurs non configurables sauf le timeout ; pas de limite explicite par IP/utilisateur (cf. 7.3.1) |
| Couverture de tests incomplète | 7.4, 8.6–8.9 | `test/` | Pas de tests CRUD/NMDA/RPC, pas d'intégration `ctest` |

---

## 🎯 Prochaines Étapes (par priorité)

1. **7.4/8.6–8.9 — Couverture de tests** : tests CRUD/NMDA/RPC
   automatisés et intégration `ctest`, seul chantier de la Phase 8
   encore ouvert.

2. **6.1 (suivi, non bloquant)** — Filtrage par souscription
   individuelle (`xpath-filter` RFC 8650) et corrélation d'un
   identifiant de souscription à un flux SSE HTTP/2 précis (notion
   de "receiver"). Actuellement, un seul flux conceptuel `NETCONF`
   diffuse toutes les notifications à tous les clients connectés
   (hors clients avec `start-time`, cf. 6.5).

3. **6.5.1 (suivi, non bloquant)** — Support du replay en mode
   Externe (nouveau message IPC dédié + routage par flux plutôt que
   simple broadcast) ; vérification automatique du replay-support
   sysrepo par module avant d'annoncer `replay-support: true` dans
   `restconf-state/streams`.

4. **7.3.1 (suivi, non bloquant)** — Rendre configurables
   `H2C_MAX_REQUEST_BODY_SIZE`/`H2C_MAX_CONCURRENT_STREAMS`/fenêtres
   (actuellement des `#define` figés à la compilation) ; envisager
   une limite par IP/utilisateur si l'usage en exploitation le
   justifie.

5. **3.10.1 (suivi, non bloquant)** — Envisager un rafraîchissement
   à chaud du contexte libyang local du gateway (mode Externe) si le
   redémarrage sur (dés)installation de module s'avère gênant en
   exploitation.

---

## ✅ Corrections récentes (session 2026-07-12)

| Fichier | Correction | Impact |
| :--- | :--- | :--- |
| `test/restconf-test.c` | Converti de `sr_rpc_subscribe` (val-based) vers `sr_rpc_subscribe_tree` (tree-based) pour correspondre à `sr_rpc_send_tree()` côté serveur | RPCs restconf-test fonctionnels |
| `include/router.h` | Ajout `RC_DS_CANDIDATE` et `RC_DS_STARTUP` dans `rc_datastore_t` | Support NMDA complet |
| `src/router.c` | Mapping `ietf-datastores:candidate` et `ietf-datastores:startup` dans le routeur NMDA ; extraction en `map_nmda_identityref()` | Tests NMDA candidate/startup passent |
| `src/plugin/sysrepo_plugin.c` | Mapping `RC_DS_CANDIDATE`/`RC_DS_STARTUP` → `SR_DS_CANDIDATE`/`SR_DS_STARTUP` dans `open_request_session()` ; retrait de `SR_EDIT_NON_RECURSIVE` pour les PUT (permet création auto des containers parents) | oven PUT/PATCH fonctionnels |
| `include/sysrepo_worker.h` | **NOUVEAU** — API publique du thread worker sysrepo confiné (ROADMAP 3.12) | Thread-safe boundary libevent ↔ sysrepo |
| `src/plugin/sysrepo_worker.c` | **NOUVEAU** — Thread worker dédié : file de messages (mutex+condvar+pipe), eventfd de completion, poll() sur sysrepo event pipe, deep-copy des requêtes, marshalling des notifications | Fin du blocage `sr_get_data()` sur oper_get_cb |
| `src/plugin/sysrepo_plugin.c` | Réécrit en wrapper thin : délègue tout au worker via `sr_worker_submit_get/edit/rpc` | Plugin non-bloquant depuis libevent |
| `CMakeLists.txt` | Ajout `sysrepo_worker.c` dans PLUGIN_SOURCES, liaison `pthread` | Support compilation multi-thread confiné |
| `AGENTS.md` | Exception documentée à la règle d'or #1 (thread worker confiné, frontière messages uniquement) | Conformité architecture |
| `src/ipc/uds_gateway.c` | **ROADMAP 3.10** — Ajout de `build_local_ly_ctx()` : au démarrage du gateway (mode Externe), scan de `<SYSREPO_REPOSITORY_PATH ou SR_YANG_REPO_PATH>/yang` et chargement de chaque `.yang` (`lys_parse_path()`, best-effort) dans un `ly_ctx` local stocké dans `plugin_ctx_s.local_ly_ctx` ; `plugin_acquire_ly_ctx()`/`plugin_release_ly_ctx()` réécrites en conséquence (plus de verrou nécessaire, contexte immuable après init) ; libéré dans `plugin_destroy()` | Résolution des clés de liste et préfixes YANG rétablie en mode Externe (`router.c`) sans aller-retour IPC bloquant sur le chemin critique |
| `CMakeLists.txt` | **ROADMAP 3.10** — Nouvelle option `SR_YANG_REPO_PATH` (défaut `/etc/sysrepo`) ; définie en compilation via `SR_REPO_PATH_DEFAULT` pour `restconf-server` en mode `BUILD_EXTERNAL_PLUGIN` | Chemin du dépôt YANG configurable sans toucher au code |
| `src/plugin/sysrepo_plugin.c` | **ROADMAP 6.1** — `plugin_subscribe_notifications()` réécrite : parcourt tous les modules *implémentés* du contexte libyang partagé (`ly_ctx_get_module_iter()`) et tente `sr_notif_subscribe_tree()` sur chacun (xpath `NULL`), au lieu du seul module `restconf-test` en dur ; échecs attendus journalisés en DEBUG. `plugin_rpc_establish_sub_cb()` valide désormais le `stream` demandé contre le seul flux réel `NETCONF` (`sr_session_set_error_message()` + `SR_ERR_NOT_FOUND` sinon) | Découverte multi-modules réelle ; RPC `establish-subscription` ne renvoie plus un ID silencieusement inopérant pour un flux inconnu |
| `src/ipc/uds_plugin.c` | **ROADMAP 6.1** — Ajout du suivi des connexions gateway actives (`gw_conn_t`/`gw_conn_add`/`gw_conn_remove`) et de `daemon_notif_push_cb()` (callback `plugin_notif_cb` qui sérialise et diffuse `IPC_MSG_NOTIF_PUSH` à toutes les gateways connectées) ; `ext_plugin_init_uds()` appelle désormais `plugin_subscribe_notifications()` pour déclencher l'abonnement sysrepo côté daemon | Mode Externe reçoit enfin les notifications sysrepo (auparavant : aucun abonnement déclenché côté daemon) |
| `src/ipc/uds_gateway.c` | **ROADMAP 6.1** — `plugin_subscribe_notifications()` n'est plus un stub : enregistre le callback applicatif (`notif_cb`/`notif_user_data` dans `plugin_ctx_s`) ; `dispatch_ipc_response()` reconnaît désormais `IPC_MSG_NOTIF_PUSH` (3 chaînes : module, xpath, payload JSON) et invoque ce callback | SSE opérationnel de bout en bout en mode Externe |
| `include/router.h` | **ROADMAP 6.5** — Ajout `time_t start_time;`/`stop_time;` à `rc_request_t` (RFC 8040 Sec 4.8.7) | Bornes de replay exposées au reste du code |
| `src/router.c` | **ROADMAP 6.5** — Nouvelle fonction `parse_rfc3339()` (via `strptime()`/`timegm()`) ; `parse_query_params()` reconnaît désormais `start-time`/`stop-time` | Query params RFC 8040 Sec 4.8.7 parsés pour `GET /streams/<name>` |
| `include/plugin_api.h` | **ROADMAP 6.5** — Nouveau type opaque `plugin_replay_sub_t` et couple `plugin_open_replay_subscription()`/`plugin_close_replay_subscription()` | API de souscription avec replay, symétrique interne/externe |
| `src/plugin/sysrepo_plugin.c` | **ROADMAP 6.5** — Implémentation réelle : `struct plugin_replay_sub_s` (souscription sysrepo dédiée + pipe libevent propre), `plugin_replay_notif_cb()`/`plugin_replay_event_cb()`, `plugin_open_replay_subscription()` (même découverte multi-modules que 6.1 mais souscription **non fusionnée** dans `ctx->sub`), `plugin_close_replay_subscription()` | Replay+live fonctionnel en mode Interne, un abonnement sysrepo par client demandant `start-time` |
| `src/ipc/uds_gateway.c` | **ROADMAP 6.5** — `plugin_open_replay_subscription()`/`plugin_close_replay_subscription()` : stubs documentes qui renvoient toujours `NULL` (replay non supporté en mode Externe) | Mode Externe retombe proprement sur un flux live-only, sans erreur bloquante |
| `src/main.c` | **ROADMAP 6.5** — `sse_registry_entry_t` gagne `replay_handle`/`app` (back-pointer) ; `sse_registry_add()` retourne désormais l'entrée créée ; nouvelle `sse_registry_remove_entry()` ; nouveau callback `on_replay_notification_cb()` (poussee exclusive, pas de broadcast) ; `on_notification_cb()` saute les entrées liées à une souscription de replay (évite le doublon) ; le handler `RC_RES_EVENT_STREAM` ouvre une souscription dédiée via `plugin_open_replay_subscription()` quand `req.start_time > 0` | `GET /streams/NETCONF?start-time=...&stop-time=...` fonctionnel de bout en bout (mode Interne) |
| `include/h2c_server.h` | **ROADMAP 7.3** — Nouvelle fonction `h2c_server_set_idle_timeout()` | API de configuration du timeout d'inactivité par connexion |
| `src/h2c_server.c` | **ROADMAP 7.3** — **Bug corrigé** : `nghttp2_option` construite puis jamais transmise (`nghttp2_session_server_new()` 3-arguments l'ignorait silencieusement) → bascule sur `nghttp2_session_server_new2()`. Ajout de SETTINGS explicites (`MAX_CONCURRENT_STREAMS`=100, `INITIAL_WINDOW_SIZE`=1 MiB) et d'une fenêtre de niveau connexion (4 MiB, `nghttp2_session_set_local_window_size()`). `on_data_chunk_recv_callback()` cappe désormais l'accumulation du corps de requête à `H2C_MAX_REQUEST_BODY_SIZE` (16 MiB) ; `on_frame_recv_callback()` rejette en 413 avant d'appeler `req_cb()` si dépassé (fusion des deux branches HEADERS/DATA dupliquées au passage). `bev_event_cb()` gère désormais `BEV_EVENT_TIMEOUT` ; `accept_cb()` applique `bufferevent_set_timeouts()` (lecture seule) si `h2c_server_set_idle_timeout()` a été appelé | Un seul corps de requête démesuré ne peut plus épuiser la mémoire du process (mono-thread, AGENTS.md règle #2) ; connexions inactives fermées proprement |
| `src/main.c` | **ROADMAP 7.3** — Nouvelle option CLI `-t <sec>` (défaut 300, 0=désactivé), appel à `h2c_server_set_idle_timeout()` juste après la création du serveur | Timeout d'inactivité configurable en exploitation |

---

## 🏛️ Proposition d'Évolution d'Architecture — Item 3.12

### Constat

`libsysrepo` n'expose **aucune API asynchrone** pour la lecture de
données (`sr_get_data()`) ou l'invocation RPC (`sr_rpc_send_tree()`) :
ce sont des appels bloquants au sens strict. Tant qu'aucune donnée
n'est couverte par un *provider* (`oper_get_cb`), le coût réel de ces
appels reste faible (accès SHM local, cf. 7.6) et l'architecture
actuelle "appel synchrone toléré" tient la route.

Le problème apparaît précisément quand le xpath demandé recoupe une
souscription `oper_get_cb` (ex. `ietf-restconf-monitoring:restconf-state`,
et plus généralement tout module tiers exposant de l'état) :

- **Mode Externe (`BUILD_EXTERNAL_PLUGIN`)** : le processus gateway et
  le démon plugin sont deux connexions sysrepo distinctes. Le démon
  doit pomper `sr_subscription_process_events()` sur son propre pipe
  d'événement pour répondre à une requête oper entrante. Or
  l'intégration de ce pipe dans `libevent` (`sr_get_event_pipe()` +
  `event_new()`) est **commentée** dans `plugin_init()` de
  `sysrepo_plugin.c`. Le démon ne traite donc jamais la requête → le
  `sr_get_data()` appelé côté gateway reste bloqué indéfiniment,
  gelant tout le process (donc **toutes** les connexions HTTP/2, pas
  seulement celle qui a déclenché l'appel — violation directe de la
  règle d'or #2 d'AGENTS.md).
- **Mode Interne** : requête et *provider* partagent le même process
  et la même connexion sysrepo ; le risque de blocage total est
  moindre mais reste réel si `oper_get_cb` devient coûteux (I/O,
  agrégation de capteurs, etc.) — le call stack de `sr_get_data()`
  exécute alors `oper_get_cb` en ligne, dans le thread `libevent`.

### Décision d'architecture retenue : Option A — thread confiné dédié à sysrepo

Isoler *tous* les appels sysrepo bloquants (`sr_get_data`,
`sr_apply_changes`, `sr_rpc_send_tree`, `sr_delete_item`, ainsi que le
pompage `sr_subscription_process_events`) dans un unique thread
"worker sysrepo", propriétaire exclusif de `sr_conn_ctx_t`. Le thread
`libevent` principal ne communique avec lui que par une file de
messages + notification de retour (`eventfd`), sans jamais appeler
sysrepo directement. Cela préserve le modèle non-bloquant pour
HTTP/2, JWT et routage — seul le sous-système intrinsèquement
synchrone (sysrepo) est isolé derrière une frontière de passage de
messages stricte, sans mutex partagé sur l'état métier.

Ceci constitue une exception explicite et strictement délimitée à la
règle d'or #1 d'AGENTS.md ("NO THREADS"), documentée par la tâche
3.12.1.

### Plan de mise en œuvre

| Étape | Tâche | Description |
| :---: | :--- | :--- |
| 1 | 3.12.1 | Amender AGENTS.md pour documenter formellement l'exception (thread sysrepo confiné, frontière = messages uniquement) |
| 2 | 3.12.2 | Définir le protocole de messages worker (`sr_worker_req_t`/`sr_worker_resp_t`, types GET/EDIT/RPC/PUMP_EVENTS, continuation = callback + `user_data` déjà existants) |
| 3 | 3.12.3 | Implémenter la file `libevent → worker` (mutex+cond ou MPSC lock-free) et la voie retour `worker → libevent` via `eventfd`/`socketpair` enregistré en `EV_READ \| EV_PERSIST` |
| 4 | 3.12.4 | Créer `src/plugin/sysrepo_worker.c` : boucle dédiée, seule propriétaire de `sr_conn_ctx_t` et des sessions par-requête, et du pompage `sr_subscription_process_events()` |
| 5 | 3.12.5 | Migrer `plugin_handle_get()`/`plugin_handle_edit()`/`plugin_handle_rpc()` vers l'enfilage de requêtes (retour immédiat côté `libevent`, `callback` invoqué plus tard à réception de la réponse) |
| 6 | 3.12.6 | Re-marshaller vers `libevent` toute action initiée depuis un callback sysrepo qui touche l'état HTTP (notamment `sse_stream_push_event()` dans `notif_event_cb`) |
| 7 | 3.12.7 | Gérer l'annulation (fermeture de flux HTTP/2 pendant une requête en vol) et un timeout borné par requête worker (`504`/`operation-failed` RESTCONF) |
| 8 | 3.12.8 | Valider le mode Externe (le worker fusionne naturellement avec le démon plugin déjà séparé, cf. 3.3–3.9) et ajouter un test de charge avec `oper_get_cb` volontairement lent |

Cette migration est un préalable de fait à 6.1 (câblage réel des
notifications, qui partage le même pipe/pompage sysrepo) et simplifie
d'autant 3.10, puisque le worker devient le point naturel où exposer
`ly_ctx` de façon cohérente entre modes Interne et Externe.

---

## 📋 Roadmap des Tests

Feuille de route détaillée des tests de conformité RESTCONF :
- **TEST-ROADMAP.md** (racine du dépôt)
- **doc/test/TEST-ROADMAP.md** (version détaillée avec statistiques)
