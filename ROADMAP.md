# 🗺️ Roadmap d'Implémentation - Backend RESTCONF h2c

> **Rappel (AGENTS.md)** : h2c uniquement (zéro TLS) · mono-thread
> `libevent` + **un seul thread worker sysrepo confiné** (exception
> documentée, cf. règle d'or #1 et item 3.12) · JWT via OpenSSL +
> Kernel Keyring (`syscall`) · plugin Interne ou Externe (IPC UDS) ·
> `application/yang-data+json|xml` · indentation **TAB** (taille 8),
> lignes ≤ **80 caractères**.

Les identifiants de tâche (`3.10`, `5.4`, ...) sont stables et
référencés depuis des `TODO`/`cf.` dans le code source : ne pas les
renuméroter. **L'historique détaillé des correctifs vit dans `git
log`** ; ce fichier ne garde que l'état courant + un journal court
des dernières sessions.

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
| **8** | Tests h2c & Intégration CTest | 90%\* | 🔴 |

*(⚪ À faire | 🟡 En cours | 🟢 Terminé | 🔴 Bloquant ; `[x]` fait,
`[~]` partiel, `[ ]` à faire)*

**État des tests (mode Interne, corrections 8.5.1 + 8.5.2 + 8.5.3 en attente de vérification) :
144/145 (99.3%) attendu** — Les correctifs apportés cette session
(plugin externe `test/restconf-test.c` chargé par `sysrepo-plugind`
pour les 6 RPCs de `restconf-test.yang` ; contrôle 409 limité aux
non-conteneurs ordinaires dans `process_edit()` ; reference counting
sur `h2c_session_t` pour prévenir le SIGSEGV use-after-free) ciblent
les 9 échecs identifiés : 5 tests RPC (`test_002`-`test_004`,
`test_007`, `test_008` de `test_rpc.py`), 3 tests payloads
volumineux (`test_008_payload_too_large` de `test_errors.py`,
`test_003`/`test_004` de `test_performance.py`), et le crash SIGSEGV
après ~86 requêtes (47 `ConnectionRefusedError`). 1 échec résiduel
attendu (`test_015_action_no_params`, bloqué par les actions YANG 1.1
commentées, cf. dette technique).

**Mode Externe : 114/145 échoués, bloquant.** La quasi-totalité de
la suite échoue en cascade car `GET
/restconf/data/ietf-yang-library:modules-state` (utilisé par le
fixture `conftest.py::_fetch_installed_modules`, appelé par presque
tous les tests) renvoie 404 au lieu de 200. Investigation : le code
de traitement (`process_get()` dans `sysrepo_worker.c`) est
identique bit-à-bit entre les deux modes (même fichier compilé dans
les deux binaires) ; l'hypothèse la plus probable est un
environnement/dépôt sysrepo différent entre les deux runs (module
`ietf-yang-library` absent/désactivé, ou `SYSREPO_REPOSITORY_PATH`
incohérent) plutôt qu'un bug de code spécifique au mode Externe —
à confirmer côté logs du démon `restconf-plugin`. Cf. entrée
« Dette technique » (**bloquant**, item 8.6).

---

## 📋 Détail des Étapes par Phase

### Phase 1 : Fondations Réseau & Boucle d'Événements — 🟢 100%
`libevent`/TCP, `nghttp2` h2c Prior Knowledge, pipe sysrepo intégré,
root discovery (`/.well-known/host-meta`), mapping headers HTTP/2,
data provider chunké. Tout `[x]`.

### Phase 2 : Sécurité, JWT & NACM — 🟢 100%
Extraction clé Kernel Keyring (`syscall`), decode/vérif JWT via
OpenSSL (`EVP_DigestVerify`), mapping NACM (`sr_session_set_user()`),
erreurs 401/403 + `ietf-restconf:errors`, parsing JSON payload JWT
(json-c). Tout `[x]`.

### Phase 3 : Architecture Plugin Sysrepo — 🟡 92%
*Fichiers : `src/plugin/sysrepo_plugin.c`, `sysrepo_worker.c`,
`plugin_main.c` (externe), `src/ipc/uds_*.c`.*

| ID | Tâche | Statut | Note |
| :--- | :--- | :---: | :--- |
| 3.1–3.4 | Dual-mode CMake, mode Interne, IPC UDS (connexion + framing) | `[x]` | - |
| 3.5 | Contexte libyang (partagé interne / local externe) | `[x]` | cf. 3.10 |
| 3.6 | Callbacks Oper Data (`rcmon`) | `[x]` | cf. 7.1 |
| 3.7 | Callbacks RPC (`sr_rpc_subscribe_tree`) | `[~]` | ID non corrélé à un flux SSE précis, cf. 6.1 |
| 3.8 | Mode Externe — dispatch IPC (GET/EDIT/RPC/NOTIF) | `[x]` | `IPC_MSG_NOTIF_PUSH` câblé (cf. 6.1) |
| 3.9 | Mode Externe — connexion sysrepo du démon | `[x]` | sessions par-requête |
| 3.10 | Mode Externe — contexte libyang **local** | `[x]` | `build_local_ly_ctx()` scanne le dépôt YANG au démarrage ; **limite** : pas de hot-reload (cf. 3.10.1) |
| 3.11 | `plugin_handle_rpc` interne | `[x]` | `sr_rpc_send_tree()` |
| **3.12** | **Thread worker sysrepo confiné** | `[x]` 🟢 | Tous les appels sysrepo bloquants isolés dans un pthread dédié (`sysrepo_worker.c`), file de messages + eventfd vers `libevent`. Exception documentée à AGENTS.md règle #1. Sous-étapes 3.12.2–3.12.8 toutes `[x]` (protocole messages, files, worker loop, migration get/edit/rpc, re-marshalling callbacks, annulation/timeout, validation externe) |

### Phase 4 : Cœur RESTCONF & CRUD (RFC 8040) — 🟢 100%
URI parsing + percent-decoding, codec JSON/XML, erreurs RESTCONF,
GET/HEAD/POST/PUT/PATCH/DELETE, API resource, RPC/action
(`lyd_parse_op` + `LYD_TYPE_RPC_RESTCONF`), query params
(`content`/`depth`/`fields`/`with-defaults`/`with-origin`),
ETag/If-Match, sélection datastore. Tout `[x]`.

### Phase 5 : Extensions NMDA (RFC 8527) — 🟢 100%
Routage `/ds/<datastore>`, `with-origin`/`with-defaults` sur
opérationnel, YANG Library 2019+ (révision réelle), restrictions par
datastore (405 lecture seule, 404 identityref inconnue), `GET
/restconf/ds` (liste). Tout `[x]`.

### Phase 6 : Notifications & SSE (RFC 8650) — 🟡 95%
| ID | Tâche | Statut | Note |
| :--- | :--- | :---: | :--- |
| 6.1 | `establish-subscription` + découverte multi-modules, `IPC_MSG_NOTIF_PUSH` bidirectionnel, filtre `?filter=` (XPath, RFC 8040 §4.8.4/6.3) | `[~]` | Un seul flux conceptuel `NETCONF` ; filtre XPath par-souscription évalué via `lyd_find_xpath()` sur le noeud de notification en mode **Interne** (cf. 6.1.1 pour la limite mode Externe) ; pas encore de corrélation ID↔flux SSE (cf. 6.1 suivi) |
| 6.2–6.4 | Ouverture stream SSE, push async, keep-alive | `[x]` | - |
| 6.5 | Replay (`start-time`/`stop-time`, RFC 3339) | `[x]` 🟢 | Souscription sysrepo dédiée par client en mode **Interne** ; **mode Externe non supporté** (fallback live-only, cf. 6.5.1) |

### Phase 7 : Monitoring & Modules YANG Conceptuels — 🟡 65%
| ID | Tâche | Statut | Note |
| :--- | :--- | :---: | :--- |
| 7.1–7.2 | `rcmon` capabilities + streams | `[x]` | - |
| 7.3 | Limitation ressources (RFC 8040 §12) | `[x]` 🟢 | SETTINGS nghttp2 explicites (`nghttp2_session_server_new2`, bug de config corrigé), cap corps requête 16 MiB → 413, timeout d'inactivité configurable (`-t`) ; valeurs figées en `#define` sauf timeout (cf. 7.3.1) |
| 7.4 | Tests conformité `nghttp` | `[ ]` | - |
| 7.5 | Module `restconf-test.yang` | `[x]` | requis par toute la suite de tests |
| 7.6 | Audit mono-thread | `[~]` | Zéro `pthread` hors worker confiné (3.12) ; jugé acceptable |

### Phase 8 : Tests h2c & Intégration CTest — 🟡 95%
Client h2c (`hyper-h2`), tests root discovery/API/HTTP methods,
suites CRUD (`test_crud.py`), NMDA (`test_nmda.py`), RPC
(`test_rpc.py`), intégration `ctest` (option
`BUILD_CTEST_INTEGRATION`, OFF par défaut). Tout `[x]` sauf :

| ID | Tâche | Statut | Note |
| :--- | :--- | :---: | :--- |
| 8.5 | Tests gestion erreurs | `[~]` | `test_008_put_modify_existing`, `test_014`/`test_015` (`test_oven.py`), `test_001_bad_request` : **confirmés passants** par exécution réelle cette session (mode Interne, 136/145). `test_015_action_no_params` toujours bloqué (actions YANG 1.1 commentées). 8 nouveaux échecs non encore investigués (RPC 500, payload trop volumineux) — cf. items 8.5.1/8.5.2 |
| **8.6** | **Mode Externe — suite de tests** | `[ ]` 🔴 | **Bloquant** : 114/145 échous en cascade, cf. État des tests ci-dessus et dette technique |

---

## ⚠️ Dette Technique Ouverte

| Sujet | Item(s) | Fichier(s) | État |
| :--- | :---: | :--- | :--- |
| **SIGSEGV : crash nghttp2 après ~86 tests — CORRECTION** | 8.5.3 | `src/h2c_server.c`, `src/main.c`, `include/h2c_server.h` | **Cause identifiée** : `SIGSEGV` dans `nghttp2_outbound_queue_pop()` après ~86 requêtes en mode Interne. Stack trace gdb montrait deux adresses de session nghttp2 **différentes** entre `h2c_send_response_with_headers()` (frame #4) et `nghttp2_session_send()` (frame #3), indiquant un **use-after-free**. Le scénario : 1) Client envoie requête GET/EDIT/RPC, 2) serveur soumet au worker thread (asynchrone), 3) client ferme la connexion (timeout/EOF), 4) `bev_event_cb()` libère `h2c_session_t` (`nghttp2_session_del` + `free`), 5) worker termine et poste completion, 6) `completion_event_cb()` appelle `get_data_cb`/`edit_data_cb`/`rpc_data_cb` avec `ctx->session` pointant vers mémoire libérée, 7) callback appelle `h2c_send_response_with_headers(ctx->session->ng_session, ...)` → **SIGSEGV**. **Fix** : reference counting sur `h2c_session_t` (`ref_count` + `connection_closed`). `h2c_session_ref()` appelé AVANT chaque soumission au worker (`plugin_handle_get`/`edit`/`rpc`), `h2c_session_unref()` appelé dans chaque callback de completion. `bev_event_cb()` marque la session comme `connection_closed=true` mais ne la libère que si `ref_count==0`. Sinon, libération différée dans `h2c_session_unref()` quand `ref_count` atteint 0. Les callbacks vérifient `h2c_session_is_alive()` avant d'envoyer la réponse (skip si connexion fermée). |
| **RPC : 5 tests — CORRECTION** | 8.5.1 | `test/restconf-test.c` (plugin externe) | **Cause identifiée** : les RPCs du module `restconf-test.yang` (get-system-status, configure-device, create-resource, set-operation-mode, process-data, trigger-event) n'avaient **aucun callback enregistré** côté sysrepo au moment du test — `sr_rpc_send_tree()` dans `process_rpc()` retournait donc une erreur sysrepo (pas de souscripteur), traduite en 500 par le worker. **Fix** : les 6 callbacks sont implémentés dans le plugin externe `test/restconf-test.c` (compilé en `.so` via `BUILD_TEST_PLUGIN=ON`), chargé par `sysrepo-plugind` via `make test-install` (cf. `test/CMakeLists.txt`). Chaque callback enregistre sa souscription via `sr_rpc_subscribe_tree()` dans `sr_plugin_init_cb()`. Le serveur RESTCONF (`sysrepo_plugin.c`) ne s'enregistre **jamais** sur les noeuds de `restconf-test.yang` — seule l'instance `sysrepo-plugind` qui charge ce plugin le fait. La validation des paramètres mandatory et des contraintes de type (range/pattern) est assurée automatiquement par sysrepo/libyang avant l'invocation du callback (`SR_ERR_LY`/`SR_ERR_INVAL_ARG` -> 400 dans `process_rpc()`). |
| **Payload volumineux : 3 tests — CORRECTION** | 8.5.2 | `src/plugin/sysrepo_worker.c` | **Cause identifiée** : le contrôle d'existence 409 (RFC 8040 §4.4 « POST sur ressource existante ») s'appliquait uniformément à tous les POST, y compris sur des conteneurs YANG (`LYS_CONTAINER`) comme `/restconf-test:interfaces`. Un POST sur un conteneur non vide (déjà peuplé par un test antérieur) renvoyait 409 au lieu d'ajouter des enfants — or 409 n'est pas dans la liste des statuts acceptés par `test_errors.py::test_008_payload_too_large` ni `test_performance.py::test_003`/`test_004`. **Fix** : dans `process_edit()`, le contrôle 409 est désormais limité aux noeuds YANG qui ne sont PAS des conteneurs ordinaires (`LYS_CONTAINER` sans le flag `LYS_PRESENCE`). Les conteneurs YANG ordinaires peuvent légitimement contenir plusieurs enfants (listes, leaf-lists) ; un POST sur un conteneur non vide ajoute simplement des enfants au lieu de retourner 409. Les conteneurs de présence (`LYS_PRESENCE`) conservent le contrôle 409 car ils représentent une entité binaire (présente/absente) plutôt qu'un parent de collection. |
| **1 test en échec confirmé** | 8.5 | `test/test_crud.py` | `test_015_action_no_params` (415) : bloqué par les actions YANG 1.1 commentées dans `restconf-test.yang` (problème libyang 5.8.6, pas un bug serveur) — inchangé. |
| **`test_008_put_modify_existing` — CONFIRMÉ** | 8.5 | `src/plugin/sysrepo_worker.c` | Cause identifiée : `process_edit()` appliquait chaque feuille individuellement via `sr_set_item_str(..., SR_EDIT_ISOLATE)` (fonction `worker_set_leaves_recursive()`, désormais supprimée) — le paramètre `default_op` ("replace" pour PUT) était calculé puis **jamais utilisé** (marqué `UNUSED`). Sur un PUT remplaçant une entrée de liste déjà existante, cette suite d'edits isolés pouvait produire plusieurs fragments de diff concurrents pour la même entrée (`interface[name='eth0']`), rejetés en validation par sysrepo (400). **Fix** : remplacement par `sr_edit_batch(sess, data, default_op)` (API sysrepo idiomate pour appliquer un arbre `lyd_node` complet en une seule opération atomique, avec l'opération par défaut correcte — "replace" pour PUT RFC 8040 §4.5, "merge" pour POST/PATCH §4.4/§4.6) suivi de `sr_apply_changes()`. **Confirmé passant par `./scripts/build_test.sh` (mode Interne) cette session.** |
| **Régression `test_oven.py::test_014`/`test_015` — CONFIRMÉE** | 8.5 | `src/plugin/sysrepo_worker.c` | Conséquence directe du fix `sr_edit_batch()` ci-dessus : une violation de contrainte de type libyang (range/pattern/length — ex. `oven:temperature` hors de `0..250`) est détectée par sysrepo **au moment de `sr_edit_batch()`** plutôt qu'à `sr_apply_changes()`, et remontée sous le code `SR_ERR_LY` (erreur libyang encapsulée) au lieu de `SR_ERR_VALIDATION_FAILED`. **Fix** : `SR_ERR_LY` ajouté au même groupe que `SR_ERR_VALIDATION_FAILED`/`SR_ERR_INVAL_ARG` → 400. **Confirmé passant cette session.** |
| **`test_001_bad_request` — CONFIRMÉ** | 8.5 | `src/plugin/sysrepo_worker.c` | `process_edit()` vérifiait "POST sur ressource existante -> 409" (RFC 8040 Sec 4.4) **avant** tout parsing du corps de la requête. **Fix** : le contrôle d'existence 409 est déplacé après le parsing réussi du corps (juste avant `sr_edit_batch()`). **Confirmé passant cette session.** |
| **RPC : 5 tests en échec (`assert 500`)** | 8.5.1 | `src/plugin/sysrepo_worker.c` (`process_rpc`), `test/test_rpc.py` | `test_002_rpc_no_params`, `test_003_rpc_with_params`, `test_004_rpc_mandatory_param`, `test_007_rpc_output`, `test_008_rpc_no_output` échouent avec un 500 en mode Interne (confirmé par exécution réelle cette session, non causé par les changements de cette session qui n'ont touché aucun chemin RPC). Cause non encore diagnostiquée : à investiguer via les logs serveur (`-v0` ou plus verbeux) pour identifier le `sr_strerror()` exact remonté par `sr_rpc_send_tree()` — candidats probables : callback RPC côté module `restconf-test.yang` manquant/non enregistré côté sysrepo, ou régression dans `codec_parse_rpc_input()`/`lyd_new_path()` sur le chemin RPC |
| **Payload trop volumineux : 3 tests en échec** | 8.5.2 | `src/h2c_server.c`, `test/test_errors.py`, `test/test_performance.py` | `test_008_payload_too_large` (`test_errors.py`), `test_003_large_payload`/`test_004_very_large_payload` (`test_performance.py`) échouent en mode Interne. À vérifier : cohérence entre le cap 16 MiB introduit en 7.3 (`H2C_MAX_REQUEST_BODY_SIZE`) et les tailles de payload attendues par ces tests (soit le cap est mal appliqué, soit les tests attendent une limite différente) |
| **Mode Externe : suite de tests bloquée (114/145)** | 8.6 | `src/plugin/sysrepo_worker.c`, `src/ipc/uds_plugin.c`, environnement | `GET /restconf/data/ietf-yang-library:modules-state` renvoie 404 au lieu de 200 en mode Externe, faisant échouer en cascade la quasi-totalité de la suite via le fixture `conftest.py::_fetch_installed_modules`. Le code de traitement (`process_get()`) est partagé à l'identique entre les deux modes (même `sysrepo_worker.c` compilé dans les deux binaires), donc la cause la plus probable n'est **pas** un bug de code spécifique au mode Externe mais un environnement sysrepo différent entre les deux runs (module `ietf-yang-library` non installé/activé pour ce run, ou `SYSREPO_REPOSITORY_PATH`/socket sysrepo incohérent entre `sysrepo-plugind` et `restconf-plugin`) — à confirmer via les logs de démarrage de `restconf-plugin` (le code retourné exact de `sr_get_data()` pour ce xpath) avant toute correction de code |
| Souscription/receiver individuel | 6.1 | `sysrepo_plugin.c`, `main.c` | Un seul flux `NETCONF` diffuse tout ; corrélation ID (retourné par `establish-subscription`) ↔ flux SSE HTTP/2 toujours absente (un client doit ouvrir lui-même `GET /streams/NETCONF`) |
| Filtre `?filter=` non appliqué en mode Externe | 6.1.1 | `uds_plugin.c`, `uds_gateway.c` | Le noeud `lyd_node` de la notification n'existe que côté daemon (connexion sysrepo) ; le fan-out SSE (qui connaît les filtres par client) vit côté gateway, sans accès à ce noeud (seul le payload sérialisé traverse l'IPC). Un filtre demandé en mode Externe est donc actuellement ignoré (notification livrée non filtrée, pas d'erreur) ; corriger nécessiterait de propager le filtre jusqu'au daemon (nouveau champ IPC) et d'y évaluer `lyd_find_xpath()` avant sérialisation, par destinataire |
| Replay mode Externe | 6.5.1 | `uds_gateway.c` | Stub `NULL` ; fallback live-only propre |
| Hot-reload contexte libyang externe | 3.10.1 | `uds_gateway.c` | Contexte figé au démarrage du gateway |
| Limites ressources non configurables | 7.3.1 | `h2c_server.c` | `H2C_MAX_REQUEST_BODY_SIZE`/`MAX_CONCURRENT_STREAMS` en `#define` ; pas de limite par IP/utilisateur |
| Tests conformité `nghttp` manuels | 7.4 | `test/` | - |

---

## 🎯 Prochaines Étapes (par priorité)

0. **3 (bloquant)** - Passer restconf:capability basic-mode de
   `report-all` à `trim`. Ajuster `codec_serialize_data_wd` et
   le comportement des tests.
0.1. **Utiliser libevent** pour gérer les taches dans
     `src/plugin/sysrepo_worker.c`
0.2. **Utiliser Bufferevents** pour géger les buffers
0.3. **SIGTERM** gérer les signeaux et terminer (libérer la
     mémoire correctement) a la reception d'un SIGTERM.
1. **8.6** — Diagnostiquer le 404 systémique du mode
   Externe sur `GET ietf-yang-library:modules-state` : inspecter les
   logs de démarrage de `restconf-plugin` et de `sysrepo-plugind`
   pour le run de test concerné, vérifier `sysrepoctl -l` (module
   `ietf-yang-library` installé/activé) et la cohérence de
   `SYSREPO_REPOSITORY_PATH` entre les deux processus, avant
   d'envisager un correctif de code.
2. **8.5.1 ✅ CORRIGÉ** — 5 tests RPC (`test_rpc.py`) en mode
   Interne : callbacks implémentés dans le plugin externe
   `test/restconf-test.c`, chargé par `sysrepo-plugind` (cf.
   `make test-install` dans `test/CMakeLists.txt`). Le serveur
   RESTCONF ne s'enregistre pas sur ces noeuds.
3. **8.5.2 ✅ CORRIGÉ** — 3 tests payloads volumineux :
   contrôle 409 limité aux non-conteneurs dans `process_edit()`.
4. **8.5.3 ✅ CORRIGÉ** — Crash SIGSEGV après ~86 requêtes :
   reference counting sur `h2c_session_t` (`ref_count` +
   `connection_closed`) pour prévenir le use-after-free quand le
   client ferme la connexion pendant qu'un callback async est en
   attente. `h2c_session_ref()` avant soumission au worker,
   `h2c_session_unref()` dans le callback de completion.
5. **8.5** — Confirmer par exécution réelle (`ctest
   -DBUILD_CTEST_INTEGRATION=ON`) les scénarios d'erreur restants.
6. **6.1 (suivi)** — Corrélation ID↔flux SSE HTTP/2 (notion de
   "receiver" RFC 8650) : le filtre XPath par-souscription
   (`?filter=`) est désormais implémenté en mode Interne ; reste la
   corrélation entre l'`id` retourné par `establish-subscription` et
   un flux SSE HTTP/2 particulier, ainsi que la propagation du
   filtre au mode Externe (cf. 6.1.1).
7. **6.5.1 (suivi)** — Replay en mode Externe (message IPC dédié) +
   vérification automatique du `replay-support` sysrepo par module.
8. **7.3.1 (suivi)** — Rendre configurables les limites HTTP/2
   (actuellement `#define`) ; limite par IP/utilisateur si besoin.
9. **3.10.1 (suivi)** — Hot-reload du contexte libyang local du
   gateway externe si le redémarrage systématique s'avère gênant.

---

## 📋 Roadmap des Tests

Feuille de route détaillée des tests de conformité RESTCONF :
- **TEST-ROADMAP.md** (racine du dépôt)
- **doc/test/TEST-ROADMAP.md** (version détaillée avec statistiques)
