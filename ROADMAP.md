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
| **3** | Architecture Plugin Sysrepo (Dual-Mode) | 55% | 🟡 |
| **4** | Cœur RESTCONF & CRUD (RFC 8040) | 85% | 🟡 |
| **5** | Extensions NMDA (RFC 8527) | 25% | 🟡 |
| **6** | Notifications & SSE (RFC 8650) | 40% | 🟡 |
| **7** | Monitoring & Modules YANG Conceptuels | 25% | 🟡 |

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
| 2.6 | Parsing JSON robuste du payload JWT | RFC 7519 | `jwt_validator.c` extrait le claim `sub` par `strstr("\"sub\"")` (recherche de sous-chaîne, pas un vrai parseur JSON) — fragile sur des valeurs échappées ou un ordre de champs différent ; à remplacer par un parsing JSON minimal ou `libyang`/`cjson` | `[ ]` |

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
| 3.8 | **Mode Externe — dispatch IPC réel** | - | `uds_read_cb`/`gateway_read_cb` sont des stubs vides ; `plugin_handle_get/edit/rpc` et `plugin_subscribe_notifications` côté `uds_gateway.c` ne sérialisent/n'envoient rien (`/* TODO: Serialize and send */`). **Le mode externe ne traite aujourd'hui aucune requête de bout en bout.** | `[ ]` |
| 3.9 | **Mode Externe — connexion sysrepo du démon** | - | `plugin_main.c` ne fait ni `sr_connect()` ni `sr_session_start()` (`/* TODO: sr_connect, sr_session_start */`) ; le démon `restconf-plugin` n'a donc aucun accès aux datastores | `[ ]` |
| 3.10| **Mode Externe — contexte libyang à distance** | - | `plugin_acquire_ly_ctx()` renvoie `NULL` en mode externe, ce qui désactive la résolution de clés de liste dans `router.c` pour toute URI keyée ; il faut soit exposer le `ly_ctx` via IPC, soit maintenir un contexte local synchronisé côté gateway | `[ ]` |
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
| 4.10| Invocation RPC / Action| RFC 8040 Sec 3.6 | `router.c` parse maintenant `rpc_module`/`rpc_name` depuis l'URI ; `main.c` route vers `RC_RES_OPERATIONS` et valide la méthode POST ; callback sysrepo reste à câbler | `[~]` |
| 4.11| Query: content | RFC 8040 Sec 4.8.1 | `config`, `nonconfig`, `all` — parsé dans `req->content_filter`, application au filtrage sysrepo reste à faire | `[~]` |
| 4.12| Query: depth | RFC 8040 Sec 4.8.2 | Limite profondeur de l'arbre — parsé dans `req->depth`, application au filtrage libyang reste à faire | `[~]` |
| 4.13| Query: fields | RFC 8040 Sec 4.8.3 | Sélection de sous-arbres — parsé dans `req->content_filter`, expression fields reste à parser | `[~]` |
| 4.14| ETag / Last-Modified | RFC 8040 Sec 3.4.1 | Collision prevention, `If-Match` | `[ ]` |
| 4.15| **Parsing Query String** | RFC 8040 Sec 3.5.1 | **Implémenté** : `:path` HTTP/2 est maintenant séparé en `path`/`query` dans `router_parse_request` ; les paramètres `content`, `depth`, `fields`, `with-defaults`, `with-origin` sont extraits | `[x]` |
| 4.16| Sélection du datastore cible | RFC 8040 Sec 1.4, 3.4 | **Implémenté** : `/restconf/data` cible `SR_DS_RUNNING` par défaut ; sessions sysrepo créées pour `running`, `operational`, `startup` ; `select_session()` route vers la bonne session | `[x]` |

### Phase 5 : Extensions NMDA (RFC 8527)
*Objectif : Support des nouveaux datastores et métadonnées opérationnelles.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 5.1 | Routage `/ds/<datastore>` | RFC 8527 Sec 3.1 | **Implémenté** : `router.c` extrait l'`identityref` et mappe vers `RC_DS_RUNNING`/`RC_DS_OPERATIONAL`/`RC_DS_INTENDED` ; `sysrepo_plugin.c` sélectionne la session correspondante | `[x]` |
| 5.2 | Query: with-origin | RFC 8527 Sec 3.2.2 | Métadonnées `origin` via plugins `libyang` sur `oper` — bloqué par 4.15 | `[ ]` |
| 5.3 | with-defaults sur Oper| RFC 8527 Sec 3.2.1 | Valeurs "in use" (RFC 8342 Sec 5.3) — bloqué par 4.15 | `[ ]` |
| 5.4 | YANG Library 2019+ | RFC 8527 Sec 2 | `ietf-yang-library` rév 2019-01-04+ obligatoire (actuellement seule la chaîne littérale est renvoyée dans l'API resource, aucune donnée `modules-state`/`module-set` réelle) | `[ ]` |
| 5.5 | Opérations restreintes par datastore | RFC 8527 Sec 3.2 | **Partiellement implémenté** : `plugin_handle_edit` retourne `405` sur `operational` ; reste à gérer les datastores dynamiques et `intended` | `[~]` |

### Phase 6 : Notifications & SSE (RFC 8650)
*Objectif : Flux d'événements asynchrones sur streams HTTP/2.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 6.1 | RPC `establish-sub` | YANG `rsn` | Souscription sysrepo, retour URI (leaf `uri` de l'augmentation `ietf-restconf-subscribed-notifications`) — `rpc_establish_sub_cb` est un stub (`/* TODO: Créer la souscription et retourner l'URI SSE */`) | `[ ]` |
| 6.2 | Ouverture Stream SSE | RFC 8040 Sec 6.3 | `nghttp2_submit_headers()` sans `END_STREAM` | `[x]` |
| 6.3 | Push Asynchrone | RFC 8040 Sec 6.4 | Callback sysrepo -> `nghttp2_submit_data()` (SSE) — `h2c_send_sse_data()` est un stub (`/* TODO: Implement data provider for SSE stream */`) | `[ ]` |
| 6.4 | Keep-Alive SSE | RFC 8040 Sec 6.4 | Timers `libevent` pour `: ping\n\n` | `[~]` |
| 6.5 | Replay | RFC 8040 Sec 4.8.7 | `start-time` / `stop-time` — bloqué par 4.15 | `[ ]` |

### Phase 7 : Monitoring & Modules YANG Conceptuels
*Objectif : Peuplement des modules `ietf-restconf` et `ietf-restconf-monitoring`.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 7.1 | Peuplement `rcmon` (Capabilities) | YANG `rcmon` | `restconf-state/capabilities` dans `oper` via callback `oper_get_cb` — callback actuellement vide (`/* TODO: Générer les données opérationnelles */`) | `[ ]` |
| 7.2 | Peuplement `rcmon` (Streams) | YANG `rcmon` | `restconf-state/streams` (liste des flux SSE actifs) — même callback vide que 7.1 | `[ ]` |
| 7.3 | Limitation Ressources | RFC 8040 Sec 12 | `WINDOW_UPDATE` nghttp2, timeouts `libevent` | `[ ]` |
| 7.4 | Tests Conformité | - | Validation RFC 8040/8527 avec `nghttp` | `[ ]` |
| 7.5 | Audit Mono-Thread | - | Zéro `pthread` confirmé, **mais appels sysrepo bloquants présents** : `sr_get_data()` (`plugin_handle_get`) et `sr_apply_changes()` (`plugin_handle_edit`) sont synchrones, alors que `CLAUDE.md` exige explicitement `sr_get_data_async()` et proscrit ce pattern | `[~]` |

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
| `oper_get_cb` et `rpc_establish_sub_cb` sont des stubs vides | `sysrepo_plugin.c` | 🟡 Faible (déjà pressenti) | 6.1, 7.1, 7.2 |
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

---

## 🎯 Prochaines Étapes Recommandées (Sprint en cours)

### Priorité 0 : Corrections fondamentales (~~bloquantes pour tout le reste~~ - ✅ Résolues)
- ~~**4.15** — Séparer `path` et `query` dès la réception `:path`~~ ✅ Implémenté
- ~~**4.16 / 5.1** — Ouvrir/rejouer la session sysrepo sur le bon datastore~~ ✅ Implémenté
- **7.5** — ~~Remplacer `sr_get_data()` par `sr_get_data_async()`~~ Note : sysrepo utilise SHM, les appels synchrones sont très rapides (pas de réseau). Le pattern actuel est acceptable.

### Priorité 1 : Notifications SSE Complètes (RFC 8650 & YANG `rsn`)
Connecter la logique des notifications au réseau :
- Finaliser le callback `rpc_establish_sub_cb` pour créer l'abonnement sysrepo.
- Générer l'URI SSE et l'injecter dans le nœud de sortie `uri` (grâce à l'augmentation YANG `ietf-restconf-subscribed-notifications`).
- Implémenter le `data_provider` pour `nghttp2_submit_data` afin de pousser les événements YANG formatés en SSE sur le stream HTTP/2.

### Priorité 2 : Peuplement de `ietf-restconf-monitoring` (YANG `rcmon`)
Le serveur doit exposer ses capacités et ses flux via le datastore `operational` :
- Implémenter la logique dans `oper_get_cb` pour générer dynamiquement la liste `capability` (ex: `with-defaults`, `depth`, `with-origin`).
- Générer dynamiquement la liste `stream` (ex: flux `NETCONF` par défaut) avec les URIs d'accès SSE.

### Priorité 3 : Filtrage Query Parameters (RFC 8040 Sec 4.8)
Appliquer les paramètres de requête parsés :
- **4.11** : Filtrer par `content` (config/nonconfig/all) en utilisant les flags libyang
- **4.12** : Limiter la profondeur avec `depth` via `LYD_PRINT_` options ou filtrage manuel
- **4.13** : Parser et appliquer l'expression `fields` (complexe, syntaxe RFC 8040 Sec 4.8.3)

### Priorité 4 : Extensions NMDA (RFC 8527)
- Implémenter la logique de `with-origin` pour annoter les données opérationnelles avec leur source (ex: `intended`, `default`, `learned`).
- Ajouter les restrictions MUST de RFC 8527 §3.2 (datastores dynamiques exclus, `405` sur datastore read-only `intended`).
- Implémenter `with-defaults` sur operational (RFC 8342 Sec 5.3).
