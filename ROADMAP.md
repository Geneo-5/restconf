# 🗺️ Roadmap d'Implémentation - Backend RESTCONF h2c

> **⚠️ Rappel des Règles de Développement (CLAUDE.md)**
> *   **Transport** : HTTP/2 Cleartext (h2c) uniquement. Zéro TLS.
> *   **Architecture** : 100% Mono-thread, non-bloquant (`libevent` + pipe `sysrepo`).
> *   **Sécurité** : JWT validé via OpenSSL, clé publique extraite du Kernel Keyring via `syscall`.
> *   **Plugin** : Compilable en mode Interne (monolithique) ou Externe (IPC via UDS).
> *   **Médias** : `application/yang-data+json` et `application/yang-data+xml`.
> *   **Formatage** : Indentation par **TABULATIONS** (taille 8), lignes limitées à **80 caractères**.
> *   **API Sysrepo** : `SR_SUBSCR_NO_THREAD` obligatoire. `sr_acquire_context()` / `sr_release_context()`.

---

## 📊 Tableau de Bord Global

| Phase | Description | Progression | Statut |
| :--- | :--- | :---: | :---: |
| **1** | Fondations Réseau & Boucle d'Événements | 100% | 🟢 |
| **2** | Sécurité, JWT & NACM | 100% | 🟢 |
| **3** | Architecture Plugin Sysrepo (Dual-Mode) | 95% | 🟢 |
| **4** | Cœur RESTCONF & CRUD (RFC 8040) | 90% | 🟢 |
| **5** | Extensions NMDA (RFC 8527) | 20% | ⚪ |
| **6** | Notifications & SSE (RFC 8650) | 40% | 🟡 |
| **7** | Monitoring & Modules YANG Conceptuels | 30% | 🟡 |

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

### Phase 3 : Architecture Plugin Sysrepo
*Objectif : Couche d'abstraction et abonnements sysrepo.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 3.1 | Config CMake Dual-Mode | - | Option `BUILD_EXTERNAL_PLUGIN` | `[x]` |
| 3.2 | Mode Interne | - | Liens statiques, appels C directs | `[x]` |
| 3.3 | Mode Externe (IPC UDS) | - | `evconnlistener` / `bufferevent` sur `AF_UNIX` | `[x]` |
| 3.4 | Protocole IPC | - | Framing Length-Header, magic `0x52434E46` | `[x]` |
| 3.5 | Contexte Libyang | - | `sr_acquire_context()` / `sr_release_context()` | `[x]` |
| 3.6 | Callbacks Oper Data | YANG `rcmon` | `sr_oper_get_subscribe` (NO_THREAD) | `[x]` |
| 3.7 | Callbacks RPC | YANG `rsn` | `sr_rpc_subscribe_tree` (NO_THREAD) | `[x]` |

### Phase 4 : Cœur RESTCONF & CRUD (RFC 8040)
*Objectif : URI parsing, Codec, et opérations CRUD complètes.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 4.1 | Parseur URI RESTCONF | RFC 8040 Sec 3.5.3 | Conversion `list=key` -> XPath via `libyang` | `[x]` |
| 4.2 | Percent-Encoding | RFC 8040 Sec 3.5.3 | Décodage des clés de listes (`%2C`, etc.) | `[x]` |
| 4.3 | Codec JSON/XML | RFC 8040 Sec 3.2, 5.2 | `lyd_print_mem()` / `lyd_parse_data_mem()` | `[x]` |
| 4.4 | Erreurs RESTCONF | RFC 8040 Sec 7.1 | `codec_serialize_errors()` JSON/XML (`yang-errors`) | `[x]` |
| 4.5 | Méthodes GET / HEAD | RFC 8040 Sec 4.2, 4.3 | `sr_get_data()`, callback asynchrone | `[x]` |
| 4.6 | Méthodes POST / PUT | RFC 8040 Sec 4.4, 4.5 | `sr_edit_batch()` (merge/replace), `201 Created` / `204 No Content` | `[x]` |
| 4.7 | Méthode PATCH | RFC 8040 Sec 4.6 | `sr_edit_batch` (merge), Plain Patch | `[x]` |
| 4.8 | Méthode DELETE | RFC 8040 Sec 4.7 | `sr_delete_item()` | `[x]` |
| 4.9 | API Resource | YANG `ietf-restconf` | `GET /restconf` (data, operations, yang-library-version) | `[x]` |
| 4.10| Invocation RPC / Action| RFC 8040 Sec 3.6 | Routage `/operations/` vers `sr_rpc_send()` | `[~]` |
| 4.11| Query: content | RFC 8040 Sec 4.8.1 | `config`, `nonconfig`, `all` | `[ ]` |
| 4.12| Query: depth | RFC 8040 Sec 4.8.2 | Limite profondeur de l'arbre | `[ ]` |
| 4.13| Query: fields | RFC 8040 Sec 4.8.3 | Sélection de sous-arbres | `[ ]` |
| 4.14| ETag / Last-Modified | RFC 8040 Sec 3.4.1 | Collision prevention, `If-Match` | `[ ]` |

### Phase 5 : Extensions NMDA (RFC 8527)
*Objectif : Support des nouveaux datastores et métadonnées opérationnelles.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 5.1 | Routage `/ds/<datastore>` | RFC 8527 Sec 3.1 | Parseur `identityref` -> `SR_DS_OPERATIONAL`, `:running`, `:intended` (via YANG `ietf-datastores`) | `[~]` |
| 5.2 | Query: with-origin | RFC 8527 Sec 3.2.2 | Métadonnées `origin` via plugins `libyang` sur `oper` | `[ ]` |
| 5.3 | with-defaults sur Oper| RFC 8527 Sec 3.2.1 | Valeurs "in use" (RFC 8342 Sec 5.3) | `[ ]` |
| 5.4 | YANG Library 2019+ | RFC 8527 Sec 2 | `ietf-yang-library` rév 2019-01-04+ obligatoire | `[ ]` |

### Phase 6 : Notifications & SSE (RFC 8650)
*Objectif : Flux d'événements asynchrones sur streams HTTP/2.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 6.1 | RPC `establish-sub` | YANG `rsn` | Souscription sysrepo, retour URI (leaf `uri` de l'augmentation `ietf-restconf-subscribed-notifications`) | `[~]` |
| 6.2 | Ouverture Stream SSE | RFC 8040 Sec 6.3 | `nghttp2_submit_headers()` sans `END_STREAM` | `[x]` |
| 6.3 | Push Asynchrone | RFC 8040 Sec 6.4 | Callback sysrepo -> `nghttp2_submit_data()` (SSE) | `[~]` |
| 6.4 | Keep-Alive SSE | RFC 8040 Sec 6.4 | Timers `libevent` pour `: ping\n\n` | `[~]` |
| 6.5 | Replay | RFC 8040 Sec 4.8.7 | `start-time` / `stop-time` | `[ ]` |

### Phase 7 : Monitoring & Modules YANG Conceptuels
*Objectif : Peuplement des modules `ietf-restconf` et `ietf-restconf-monitoring`.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 7.1 | Peuplement `rcmon` (Capabilities) | YANG `rcmon` | `restconf-state/capabilities` dans `oper` via callback `oper_get_cb` | `[~]` |
| 7.2 | Peuplement `rcmon` (Streams) | YANG `rcmon` | `restconf-state/streams` (liste des flux SSE actifs) | `[~]` |
| 7.3 | Limitation Ressources | RFC 8040 Sec 12 | `WINDOW_UPDATE` nghttp2, timeouts `libevent` | `[ ]` |
| 7.4 | Tests Conformité | - | Validation RFC 8040/8527 avec `nghttp` | `[ ]` |
| 7.5 | Audit Mono-Thread | - | Zéro `pthread`, Zéro appel bloquant | `[x]` |

---

## 🎯 Prochaines Étapes Recommandées (Sprint en cours)

### Priorité 1 : Notifications SSE Complètes (RFC 8650 & YANG `rsn`)
Connecter la logique des notifications au réseau :
- Finaliser le callback `rpc_establish_sub_cb` pour créer l'abonnement sysrepo.
- Générer l'URI SSE et l'injecter dans le nœud de sortie `uri` (grâce à l'augmentation YANG `ietf-restconf-subscribed-notifications`).
- Implémenter le `data_provider` pour `nghttp2_submit_data` afin de pousser les événements YANG formatés en SSE sur le stream HTTP/2.

### Priorité 2 : Peuplement de `ietf-restconf-monitoring` (YANG `rcmon`)
Le serveur doit exposer ses capacités et ses flux via le datastore `operational` :
- Implémenter la logique dans `oper_get_cb` pour générer dynamiquement la liste `capability` (ex: `with-defaults`, `depth`, `with-origin`).
- Générer dynamiquement la liste `stream` (ex: flux `NETCONF` par défaut) avec les URIs d'accès SSE.

### Priorité 3 : Extensions NMDA (RFC 8527)
- Mapper correctement les `identityref` de l'URI `/restconf/ds/ietf-datastores:operational` vers les datastores sysrepo.
- Implémenter la logique de `with-origin` pour annoter les données opérationnelles avec leur source (ex: `intended`, `default`, `learned`).