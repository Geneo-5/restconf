# 🗺️ Roadmap d'Implémentation - Backend RESTCONF h2c (Mise à jour)

> **⚠️ Rappel des Règles de Développement (CLAUDE.md)**
> *   **Transport** : HTTP/2 Cleartext (h2c) uniquement. Zéro TLS.
> *   **Architecture** : 100% Mono-thread, non-bloquant (`libevent` + FD `sysrepo`).
> *   **Sécurité** : JWT validé via OpenSSL avec clé publique extraite du Kernel Keyring.
> *   **Plugin** : Compilable en mode Interne (monolithique) ou Externe (IPC via UDS).
> *   **Formatage** : Indentation par **TABULATIONS** (taille 8), lignes limitées à **80 caractères**.

---

## 📊 Tableau de Bord Global

| Phase | Description | Progression | Statut Global |
| :--- | :--- | :---: | :---: |
| **1** | Fondations Réseau & Boucle d'Événements | 80% | 🟢 |
| **2** | Sécurité, JWT & NACM | 80% | 🟢 |
| **3** | Architecture Plugin Sysrepo (Dual-Mode) | 90% | 🟢 |
| **4** | Cœur RESTCONF (RFC 8040) | 30% | 🟡 |
| **5** | Extensions NMDA (RFC 8527) | 10% | ⚪ |
| **6** | Notifications & SSE (RFC 8650) | 60% | 🟡 |
| **7** | Monitoring, Conformité & Optimisations | 20% | ⚪ |

*(Légende : ⚪ À faire | 🟡 En cours | 🟢 Terminé / Scaffoldé)*

---

## 📋 Détail des Étapes par Phase

### Phase 1 : Fondations Réseau & Boucle d'Événements
*Objectif : Serveur TCP h2c non-bloquant et intégration native des FD sysrepo dans libevent.*

| ID | Tâche / Étape | Référence RFC / YANG | Détails Techniques (Mono-Thread / h2c) | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 1.1 | Init `libevent` & TCP Listener | - | `event_base_new()`, `evconnlistener_new_bind()` (`h2c_server.c`) | `[x]` |
| 1.2 | Moteur HTTP/2 Cleartext (h2c) | RFC 8040 Sec 2 | `nghttp2_session_server_new()` en mode "Prior Knowledge" (`h2c_server.c`) | `[x]` |
| 1.3 | Intégration FD sysrepo | - | `sr_get_event_fd()` ajouté à `libevent` via `event_new(EV_READ)` (`sysrepo_plugin.c`) | `[x]` |
| 1.4 | Root Discovery | RFC 8040 Sec 3.1 | Routage statique `/.well-known/host-meta` (XRD) et `/restconf` (Routing dans `router.c`, payload XRD à générer) | `[~]` |
| 1.5 | Mapping Headers HTTP/2 | RFC 8040 Sec 5 | Extraction `:method`, `:path`, `Authorization` via callbacks `nghttp2` (`h2c_server.c`) | `[x]` |

### Phase 2 : Sécurité, JWT & NACM
*Objectif : Authentification asynchrone sans thread, validation crypto en mémoire.*

| ID | Tâche / Étape | Référence RFC / YANG | Détails Techniques (Mono-Thread / h2c) | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 2.1 | Extraction clé Keyring | - | syscall(__NR_request_key) / syscall(__NR_keyctl) au démarrage (jwt_validator.c) | `[x]` |
| 2.2 | Parsing JWT en mémoire | - | Chargement clé dans `EVP_PKEY` (OpenSSL). Vérification CPU pur (`jwt_validator.c`) | `[x]` |
| 2.3 | Mapping NACM | RFC 8040 Sec 4 | Extraction claim `sub` -> `sr_session_set_user()` (À câbler dans le flux asynchrone du plugin) | `[~]` |
| 2.4 | Gestion erreurs Auth | RFC 8040 Sec 7 | Retour HTTP 401/403 avec `yang-errors` (`access-denied`) (`main.c`) | `[x]` |

### Phase 3 : Architecture Plugin Sysrepo (Dual-Mode)
*Objectif : Couche d'abstraction pour basculer entre mode Interne et Externe (UDS).*

| ID | Tâche / Étape | Référence RFC / YANG | Détails Techniques (Mono-Thread / h2c) | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 3.1 | Config CMake Dual-Mode | - | Option `BUILD_EXTERNAL_PLUGIN` (ON/OFF) (`CMakeLists.txt`) | `[x]` |
| 3.2 | Mode Interne | - | Liens statiques, appels C directs, même espace mémoire (`sysrepo_plugin.c`) | `[x]` |
| 3.3 | Mode Externe (IPC UDS) | - | `evconnlistener` (Plugin) / `bufferevent` (Gateway) sur `AF_UNIX` (`uds_*.c`) | `[x]` |
| 3.4 | Protocole IPC | - | Framing Length-Header (`ipc_msg_header_t`), sérialisation (`uds_common.c`) | `[x]` |
| 3.5 | Callbacks RPC Plugin | YANG `rsn` | `sr_rpc_subscribe` pour `establish-subscription` | `[~]` |
| 3.6 | Callbacks Oper Data | YANG `rcmon` | `sr_oper_get_subscribe` pour `ietf-restconf-monitoring` | `[~]` |

### Phase 4 : Cœur RESTCONF (RFC 8040)
*Objectif : Implémentation des méthodes CRUD, RPC et gestion des erreurs.*

| ID | Tâche / Étape | Référence RFC / YANG | Détails Techniques (Mono-Thread / h2c) | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 4.1 | Parseur URI RESTCONF | RFC 8040 Sec 3.5 | Conversion `list=key1,key2` vers XPath `libyang` (Structure de base dans `router.c`, percent-encoding à finaliser) | `[~]` |
| 4.2 | Méthodes GET / HEAD | RFC 8040 Sec 4.3 | `sr_get_data_async()`, gestion `content`, `depth`, `fields` (Routing OK, appels sysrepo à écrire) | `[~]` |
| 4.3 | Méthodes POST / PUT | RFC 8040 Sec 4.4/4.5| `sr_edit_batch()`, `sr_set_item()`, gestion `insert`/`point` | `[ ]` |
| 4.4 | Méthodes PATCH / DELETE| RFC 8040 Sec 4.6/4.7| `sr_edit_batch(merge)`, `sr_delete_item()` | `[ ]` |
| 4.5 | Invocation RPC / Action | RFC 8040 Sec 3.6 | `sr_rpc_send()`, `sr_action_send()` | `[~]` |
| 4.6 | Formatage Erreurs | RFC 8040 Sec 7.1 | Génération `ietf-restconf:errors` (JSON/XML) via `libyang` (Basique dans `main.c`, à enrichir) | `[~]` |

### Phase 5 : Extensions NMDA (RFC 8527)
*Objectif : Support des nouveaux datastores et métadonnées opérationnelles.*

| ID | Tâche / Étape | Référence RFC / YANG | Détails Techniques (Mono-Thread / h2c) | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 5.1 | Routage Datastores NMDA | RFC 8527 Sec 3.1 | Parseur `/restconf/ds/<identity>` -> `SR_DS_OPERATIONAL`, etc. (Enum présente dans `router.c`) | `[~]` |
| 5.2 | Query Param `with-origin`| RFC 8527 Sec 3.2.2 | Injection métadonnées `origin` via plugins `libyang` sur `oper` | `[ ]` |
| 5.3 | `with-defaults` sur Oper | RFC 8527 Sec 3.2.1 | Adaptation sémantique pour les valeurs "in use" | `[ ]` |
| 5.4 | Conflits d'édition | RFC 8040 Sec 3.4.1 | Gestion `ETag`, `Last-Modified`, `If-Match`, `If-Unmodified-Since`| `[ ]` |

### Phase 6 : Notifications & SSE (RFC 8650)
*Objectif : Flux d'événements asynchrones sur streams HTTP/2 persistants.*

| ID | Tâche / Étape | Référence RFC / YANG | Détails Techniques (Mono-Thread / h2c) | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 6.1 | RPC `establish-sub` | YANG `rsn` | Création souscription `sysrepo`, retour URI SSE (leaf `uri`) | `[~]` |
| 6.2 | Ouverture Stream SSE | RFC 8040 Sec 6.3 | `nghttp2_submit_headers()` sans flag `END_STREAM` (`sse_stream.c`) | `[x]` |
| 6.3 | Push Asynchrone | RFC 8040 Sec 6.4 | Callback FD sysrepo -> `nghttp2_submit_data()` (SSE format) (`sse_stream.c`) | `[x]` |
| 6.4 | Keep-Alive SSE | - | Timers `libevent` pour envoyer `: ping\n\n` périodiquement (`sse_stream.c`) | `[x]` |
| 6.5 | Replay (Optionnel) | RFC 8040 Sec 4.8.7 | Gestion `start-time` / `stop-time` via historique `sysrepo` | `[ ]` |

### Phase 7 : Monitoring, Conformité & Optimisations
*Objectif : Finalisation, respect strict des RFCs et robustesse.*

| ID | Tâche / Étape | Référence RFC / YANG | Détails Techniques (Mono-Thread / h2c) | Statut |
| :--- | :--- | :--- | :--- | :---: |
| 7.1 | Peuplement `rcmon` | YANG `rcmon` | Génération dynamique `capabilities` et `streams` dans `oper` | `[~]` |
| 7.2 | YANG Library | RFC 8527 Sec 2 | Exposition `ietf-yang-library` (rév 2019-01-04+) | `[ ]` |
| 7.3 | Limitation Ressources | - | Config `WINDOW_UPDATE` nghttp2, timeouts `libevent` | `[ ]` |
| 7.4 | Tests Conformité | - | Validation avec `nghttp`, `sysrepocfg`, clients RESTCONF | `[ ]` |
| 7.5 | Audit Mono-Thread | - | Vérification stricte : Zéro `pthread`, Zéro appel bloquant | `[~]` |

---

### 🎯 Prochaines étapes recommandées (Sprint en cours)

L'ossature (le "squelette") du serveur est maintenant **solide et complète** (Phases 1, 2 et 3). Les fondations réseau, la sécurité sans thread, et la dualité Interne/Externe sont en place.

Pour la suite, je vous recommande de vous concentrer sur la **Phase 4 (Cœur RESTCONF)** :
1.  **Finaliser le parseur d'URI (`router.c`)** : C'est le pont critique entre HTTP et sysrepo. Il faut écrire la logique qui transforme `/restconf/data/ietf-interfaces:interfaces/interface=eth0` en XPath sysrepo `/ietf-interfaces:interfaces/interface[name='eth0']`, en gérant le percent-encoding.
2.  **Implémenter les appels sysrepo asynchrones** : Connecter les méthodes HTTP (GET, POST, etc.) aux fonctions `sr_get_data_async` et `sr_edit_batch` dans le plugin, et s'assurer que les callbacks de réponse génèrent correctement les payloads JSON/XML via `libyang` (`lyd_print_data`).