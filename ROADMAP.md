# 🗺️ Roadmap d'Implémentation - Backend RESTCONF h2c

> **⚠️ Rappel des Règles de Développement (CLAUDE.md)**
> *   **Transport** : HTTP/2 Cleartext (h2c) uniquement. Zéro TLS.
> *   **Architecture** : 100% Mono-thread, non-bloquant
>     (`libevent` + FD `sysrepo`).
> *   **Sécurité** : JWT validé via OpenSSL, clé publique extraite
>     du Kernel Keyring via `syscall(__NR_request_key/keyctl)`.
> *   **Plugin** : Compilable en mode Interne (monolithique)
>     ou Externe (IPC via UDS).
> *   **Médias** : `application/yang-data+json` et
>     `application/yang-data+xml` (RFC 8040 Sec 3.2).
> *   **Formatage** : Indentation par **TABULATIONS** (taille 8),
>     lignes limitées à **80 caractères**.
> *   **API Sysrepo** : Utiliser `SR_SUBSCR_NO_THREAD` pour tous
>     les abonnements. Utiliser `sr_acquire_context()` /
>     `sr_release_context()` pour le contexte libyang.

---

## 📊 Tableau de Bord Global

| Phase | Description | Progression | Statut |
| :--- | :--- | :---: | :---: |
| **1** | Fondations Réseau & Boucle d'Événements | 90% | 🟢 |
| **2** | Sécurité, JWT & NACM | 95% | 🟢 |
| **3** | Architecture Plugin Sysrepo (Dual-Mode) | 75% | 🟡 |
| **4** | Cœur RESTCONF & Encodage (RFC 8040) | 55% | 🟡 |
| **5** | Extensions NMDA (RFC 8527) | 10% | ⚪ |
| **6** | Notifications & SSE (RFC 8650) | 45% | 🟡 |
| **7** | Monitoring, Conformité & Optimisations | 20% | ⚪ |

*(Légende : ⚪ À faire | 🟡 En cours | 🟢 Terminé)*

---

## 📋 Détail des Étapes par Phase

### Phase 1 : Fondations Réseau & Boucle d'Événements
*Objectif : Serveur TCP h2c non-bloquant et intégration
des FD sysrepo dans libevent.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 1.1 | Init `libevent` & TCP Listener | - | `event_base_new()`, `evconnlistener_new_bind()` | `[x]` |
| 1.2 | Moteur HTTP/2 Cleartext (h2c) | RFC 8040 Sec 2 | `nghttp2_session_server_new()` mode Prior Knowledge | `[x]` |
| 1.3 | Intégration FD sysrepo | - | `sr_get_event_pipe()` avec `SR_SUBSCR_NO_THREAD`, ajouté à `libevent` via `event_new(EV_READ \| EV_PERSIST)` | `[x]` |
| 1.4 | Root Discovery | RFC 8040 Sec 3.1 | Routage `/.well-known/host-meta` (XRD) et `/restconf` | `[~]` |
| 1.5 | Mapping Headers HTTP/2 | RFC 8040 Sec 5 | Extraction `:method`, `:path`, `Authorization`, `Content-Type`, `Accept` | `[x]` |
| 1.6 | Macro MAKE_NV | - | Helper pour `nghttp2_nv` dans `h2c_server.c` | `[x]` |
| 1.7 | Data Provider HTTP/2 | - | `nghttp2_data_provider` avec `data_read_callback` pour les réponses | `[x]` |

### Phase 2 : Sécurité, JWT & NACM
*Objectif : Authentification asynchrone sans thread,
validation crypto en mémoire.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 2.1 | Extraction clé Keyring | - | `syscall(__NR_request_key)` / `syscall(__NR_keyctl)` au boot | `[x]` |
| 2.2 | Base64URL Decode | RFC 7515 | Conversion Base64URL->Base64 + `EVP_DecodeBlock` (OpenSSL) | `[x]` |
| 2.3 | Vérification JWT | RFC 7519 | `EVP_DigestVerifyInit/Update/Final` en mémoire (CPU pur) | `[x]` |
| 2.4 | Mapping NACM | RFC 8040 Sec 4 | Extraction claim `sub` -> `sr_session_set_user()` | `[~]` |
| 2.5 | Gestion erreurs Auth | RFC 8040 Sec 7 | Retour HTTP 401/403 avec `ietf-restconf:errors` | `[x]` |

### Phase 3 : Architecture Plugin Sysrepo (Dual-Mode)
*Objectif : Couche d'abstraction pour basculer entre
mode Interne et Externe (UDS).*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 3.1 | Config CMake Dual-Mode | - | Option `BUILD_EXTERNAL_PLUGIN` (ON/OFF) | `[x]` |
| 3.2 | Mode Interne | - | Liens statiques, appels C directs | `[x]` |
| 3.3 | Mode Externe (IPC UDS) | - | `evconnlistener` (Plugin) / `bufferevent` (Gateway) sur `AF_UNIX` | `[~]` |
| 3.4 | Protocole IPC | - | Framing Length-Header (`ipc_msg_header_t`), magic `0x52434E46` | `[~]` |
| 3.5 | Callbacks RPC Plugin | YANG `rsn` | `sr_rpc_subscribe_tree` avec `SR_SUBSCR_NO_THREAD` | `[x]` |
| 3.6 | Callbacks Oper Data | YANG `rcmon` | `sr_oper_get_subscribe` avec `SR_SUBSCR_NO_THREAD` | `[x]` |
| 3.7 | Contexte Libyang | - | `sr_acquire_context()` / `sr_release_context()` obligatoire | `[x]` |

### Phase 4 : Cœur RESTCONF & Encodage (RFC 8040)
*Objectif : CRUD, RPC, et gestion des médias.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 4.1 | Parseur URI RESTCONF | RFC 8040 Sec 3.5.3 | Conversion `list=key` vers XPath, percent-encoding | `[~]` |
| 4.2 | Codec JSON/XML | RFC 8040 Sec 3.2, 5.2, 11.3 | `lyd_print_mem()` / `lyd_parse_data_mem()` via libyang | `[x]` |
| 4.3 | Négociation Accept | RFC 8040 Sec 3.2 | Header `Accept` -> `MEDIA_TYPE_JSON` ou `MEDIA_TYPE_XML` | `[x]` |
| 4.4 | Erreurs RESTCONF | RFC 8040 Sec 7.1 | `codec_serialize_errors()` JSON/XML (`ietf-restconf:errors`) | `[x]` |
| 4.5 | Méthodes GET / HEAD | RFC 8040 Sec 4.2, 4.3 | `sr_get_data()`, callback asynchrone `get_data_cb` | `[~]` |
| 4.6 | Méthodes POST / PUT | RFC 8040 Sec 4.4, 4.5 | `sr_edit_batch()`, `sr_set_item()` | `[ ]` |
| 4.7 | Méthode PATCH | RFC 8040 Sec 4.6 | `sr_edit_batch` merge, `Accept-Patch` | `[ ]` |
| 4.8 | Méthode DELETE | RFC 8040 Sec 4.7 | `sr_delete_item()` | `[ ]` |
| 4.9 | RPC / Action | RFC 8040 Sec 3.6 | Routage `/restconf/operations/` vers `sr_rpc_send()` | `[~]` |
| 4.10 | Query: content | RFC 8040 Sec 4.8.1 | `config`, `nonconfig`, `all` | `[ ]` |
| 4.11 | Query: depth | RFC 8040 Sec 4.8.2 | Limite profondeur de l'arbre retourné | `[ ]` |
| 4.12 | Query: fields | RFC 8040 Sec 4.8.3 | Sélection de sous-arbres | `[ ]` |
| 4.13 | ETag / Last-Modified | RFC 8040 Sec 3.4.1, 3.5.1 | Collision prevention, `If-Match`, `If-Unmodified-Since` | `[ ]` |

### Phase 5 : Extensions NMDA (RFC 8527)
*Objectif : Support des nouveaux datastores et
métadonnées opérationnelles.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 5.1 | Routage `/ds/<datastore>` | RFC 8527 Sec 3.1 | Parseur `identityref` -> `SR_DS_OPERATIONAL`, etc. | `[~]` |
| 5.2 | Query: with-origin | RFC 8527 Sec 3.2.2 | Métadonnées `origin` via plugins `libyang` sur `oper` | `[ ]` |
| 5.3 | with-defaults sur Oper | RFC 8527 Sec 3.2.1 | Valeurs "in use" | `[ ]` |
| 5.4 | YANG Library 2019-01-04 | RFC 8527 Sec 2 | `ietf-yang-library` rév 2019-01-04+ obligatoire | `[ ]` |

### Phase 6 : Notifications & SSE (RFC 8650)
*Objectif : Flux d'événements asynchrones sur
streams HTTP/2 persistants.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 6.1 | RPC `establish-sub` | YANG `rsn` | Souscription sysrepo, retour URI SSE (leaf `uri`) | `[~]` |
| 6.2 | Ouverture Stream SSE | RFC 8040 Sec 6.3 | `nghttp2_submit_headers()` sans `END_STREAM` | `[x]` |
| 6.3 | Push Asynchrone | RFC 8040 Sec 6.4 | Callback sysrepo -> `nghttp2_submit_data()` (SSE) | `[~]` |
| 6.4 | Keep-Alive SSE | RFC 8040 Sec 6.4 | Timers `libevent` pour `: ping\n\n` | `[~]` |
| 6.5 | Replay | RFC 8040 Sec 4.8.7 | `start-time` / `stop-time` | `[ ]` |

### Phase 7 : Monitoring, Conformité & Optimisations
*Objectif : Finalisation et robustesse.*

| ID | Tâche | Référence | Détails Techniques | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 7.1 | Peuplement `rcmon` | YANG `rcmon` | `capabilities` et `streams` dans `oper` | `[~]` |
| 7.2 | Limitation Ressources | - | `WINDOW_UPDATE` nghttp2, timeouts `libevent` | `[ ]` |
| 7.3 | Tests Conformité | - | Validation RFC 8040/8527 avec `nghttp` | `[ ]` |
| 7.4 | Audit Mono-Thread | - | Zéro `pthread`, Zéro appel bloquant | `[~]` |

---

## 📁 Structure Actuelle des Fichiers

```
restconf-h2c-backend/
├── CMakeLists.txt
├── Dockerfile
├── CLAUDE.md
├── ROADMAP.md
├── README.md
├── include/
│   ├── h2c_server.h
│   ├── router.h
│   ├── jwt_validator.h
│   ├── sse_stream.h
│   ├── codec.h
│   ├── plugin_api.h
│   └── ipc/
│       └── uds_common.h
└── src/
    ├── main.c
    ├── h2c_server.c
    ├── router.c
    ├── jwt_validator.c
    ├── sse_stream.c
    ├── codec.c
    ├── ipc/
    │   ├── uds_common.c
    │   ├── uds_gateway.c
    │   └── uds_plugin.c
    └── plugin/
        ├── sysrepo_plugin.c
        ├── rpc_handlers.c
        ├── oper_data.c
        └── plugin_main.c
```

---

## 🎯 Prochaines Étapes Recommandées

### Priorité 1 : Parseur d'URI RESTCONF (`router.c`)
Le pont critique entre HTTP et sysrepo. Il faut :
- Convertir `/restconf/data/module:container/list=key`
  en XPath `/module:container/list[name='key']`.
- Gérer le percent-encoding (RFC 8040 Sec 3.5.3).
- Gérer les clés composées (`list=key1,key2`).

### Priorité 2 : Opérations d'Édition (`plugin_handle_edit`)
- Connecter POST/PUT/PATCH/DELETE aux API sysrepo.
- Parser le body entrant via `codec_parse_data()`.
- Utiliser `sr_edit_batch()` pour les modifications.

### Priorité 3 : Notifications SSE Complètes
- Connecter les callbacks sysrepo aux streams HTTP/2.
- Implémenter le data provider pour `nghttp2_submit_data`.
- Gérer les timers keep-alive via `libevent`.