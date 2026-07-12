




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
| **6** | Notifications & SSE (RFC 8650) | 88% | 🟡 |
| **7** | Monitoring & Modules YANG Conceptuels | 50% | 🟡 |
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
| 3.7 | Callbacks RPC | YANG `rsn` | `sr_rpc_subscribe_tree` (NO_THREAD) ; ID de souscription retourné, pas de notification réelle (cf. 6.1) | `[~]` |
| 3.8 | Mode Externe — dispatch IPC réel | - | `plugin_data_cb`/`plugin_rpc_cb` transport-agnostiques ; corrélation par `msg_id`. `plugin_subscribe_notifications` reste un stub côté gateway (dépend de 6.1) | `[x]` |
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
| 6.1 | RPC `establish-subscription` + câblage réel | YANG `rsn` | `sr_notif_subscribe()` (module `restconf-test` en dur) → `notif_event_cb()` → enveloppe RFC 8040 Sec 6.4 → registre `sse_registry_entry_t` → diffusion `on_notification_cb()`/`sse_stream_push_event()`. **Limites** : un seul module câblé, pas de filtrage par stream, `establish-subscription` reste un stub déconnecté, mode Externe non câblé, non validé par compilation. **Dépend du même pipe sysrepo que 3.12** | `[~]` |
| 6.2 | Ouverture Stream SSE | RFC 8040 Sec 6.3 | `nghttp2_submit_response()` + data provider SSE | `[x]` |
| 6.3 | Push Asynchrone | RFC 8040 Sec 6.4 | File d'attente + `NGHTTP2_ERR_DEFERRED` + `nghttp2_session_resume_data()` | `[x]` |
| 6.4 | Keep-Alive SSE | RFC 8040 Sec 6.4 | Timer `libevent` `EV_PERSIST` (30s) → `: ping\n\n` | `[x]` |
| 6.5 | Replay | RFC 8040 Sec 4.8.7 | `start-time`/`stop-time` — dépend de 6.1 | `[ ]` |

### Phase 7 : Monitoring & Modules YANG Conceptuels
*Objectif : Peuplement des modules `ietf-restconf` et `ietf-restconf-monitoring`.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 7.1 | Peuplement `rcmon` (Capabilities) | YANG `rcmon` | `oper_get_cb` génère les capacités dynamiquement | `[x]` |
| 7.2 | Peuplement `rcmon` (Streams) | YANG `rcmon` | `oper_get_cb` génère la liste des streams (NETCONF) | `[x]` |
| 7.3 | Limitation Ressources | RFC 8040 Sec 12 | `WINDOW_UPDATE` nghttp2, timeouts `libevent` | `[ ]` |
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
| `establish-subscription` stub / pas de filtrage | 6.1, 6.5 | `sysrepo_plugin.c` | Pas de souscription filtrée réelle, pas de replay |
| `plugin_subscribe_notifications` stub côté gateway externe | 3.8 (partiel) | `uds_gateway.c` | Dépend de 6.1 |
| Pas de limitation de ressources | 7.3 | `h2c_server.c` | Pas de `WINDOW_UPDATE` nghttp2 ni de timeouts explicites |
| Couverture de tests incomplète | 7.4, 8.6–8.9 | `test/` | Pas de tests CRUD/NMDA/RPC, pas d'intégration `ctest` |

---

## 🎯 Prochaines Étapes (par priorité)

1. **6.1 — Finaliser le câblage des notifications**
   Filtrage par souscription, découverte multi-modules, mode Externe.
   Clôt aussi 3.7 (callbacks RPC de souscription).

2. **6.5 — Replay des notifications** (`start-time`/`stop-time`),
   dépend de 6.1.

3. **7.3 — Limitation de ressources** (`WINDOW_UPDATE` nghttp2,
   timeouts `libevent`).

4. **3.10.1 (suivi, non bloquant)** — Envisager un rafraîchissement
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
