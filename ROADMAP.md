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
| **8** | Tests h2c & Intégration CTest | 95%\* | 🟡 |

*(⚪ À faire | 🟡 En cours | 🟢 Terminé | 🔴 Bloquant ; `[x]` fait,
`[~]` partiel, `[ ]` à faire)*

**État des tests (dernier run connu, 145 tests) : 143/145 (98.6%)
attendu, à confirmer.** 1 échec ouvert restant (`test_015_action_no_params`,
bloqué par les actions YANG 1.1 commentées, cf. « Dette technique »).
`test_008_put_modify_existing` (test_crud.py),
`test_014_temperature_range`/`test_015_temperature_minimum`
(test_oven.py) et `test_001_bad_request` (test_errors.py) corrigés
cette session (cf. journal ci-dessous) — **à confirmer par
`./scripts/build_test.sh`**.

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
| 6.1 | `establish-subscription` + découverte multi-modules, `IPC_MSG_NOTIF_PUSH` bidirectionnel | `[~]` | Un seul flux conceptuel `NETCONF` ; pas de `xpath-filter` par souscription individuelle ni de corrélation ID↔flux SSE (cf. 6.1 suivi) |
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
| 8.5 | Tests gestion erreurs | `[~]` | `test_008_put_modify_existing` : cause identifiée et corrigée cette session (cf. journal), à reconfirmer par exécution réelle. `test_015_action_no_params` toujours bloqué (actions YANG 1.1 commentées). Revue de code seule pour le reste ; à confirmer par exécution réelle |

---

## ⚠️ Dette Technique Ouverte

| Sujet | Item(s) | Fichier(s) | État |
| :--- | :---: | :--- | :--- |
| **1 test en échec confirmé** | 8.5 | `test/test_crud.py` | `test_015_action_no_params` (415) : bloqué par les actions YANG 1.1 commentées dans `restconf-test.yang` (problème libyang 5.8.6, pas un bug serveur) — inchangé. |
| **`test_008_put_modify_existing` — corrigé (à reconfirmer)** | 8.5 | `src/plugin/sysrepo_worker.c` | Cause identifiée : `process_edit()` appliquait chaque feuille individuellement via `sr_set_item_str(..., SR_EDIT_ISOLATE)` (fonction `worker_set_leaves_recursive()`, désormais supprimée) — le paramètre `default_op` ("replace" pour PUT) était calculé puis **jamais utilisé** (marqué `UNUSED`). Sur un PUT remplaçant une entrée de liste déjà existante, cette suite d'edits isolés pouvait produire plusieurs fragments de diff concurrents pour la même entrée (`interface[name='eth0']`), rejetés en validation par sysrepo (400). **Fix** : remplacement par `sr_edit_batch(sess, data, default_op)` (API sysrepo idiomate pour appliquer un arbre `lyd_node` complet en une seule opération atomique, avec l'opération par défaut correcte — "replace" pour PUT RFC 8040 §4.5, "merge" pour POST/PATCH §4.4/§4.6) suivi de `sr_apply_changes()`. Corrige potentiellement aussi la sémantique de remplacement PUT (jamais réellement honorée avant ce fix). **À confirmer par `./scripts/build_test.sh`.** |
| **Régression `test_oven.py::test_014`/`test_015` — corrigée (même session)** | 8.5 | `src/plugin/sysrepo_worker.c` | Conséquence directe du fix `sr_edit_batch()` ci-dessus : une violation de contrainte de type libyang (range/pattern/length — ex. `oven:temperature` hors de `0..250`) est détectée par sysrepo **au moment de `sr_edit_batch()`** (fusion du batch dans l'arbre d'édition de la session) plutôt qu'à `sr_apply_changes()`, et remontée sous le code `SR_ERR_LY` (erreur libyang encapsulée) au lieu de `SR_ERR_VALIDATION_FAILED`. Le mapping erreur→HTTP de `process_edit()` ne connaissait pas `SR_ERR_LY` et retombait sur le cas générique 500. **Fix** : `SR_ERR_LY` ajouté au même groupe que `SR_ERR_VALIDATION_FAILED`/`SR_ERR_INVAL_ARG` → 400 (RFC 8040 Sec 7, données non conformes au schéma). **À confirmer par `./scripts/build_test.sh`.** |
| **`test_001_bad_request` — corrigé (bug préexistant, sans rapport avec les fix ci-dessus)** | 8.5 | `src/plugin/sysrepo_worker.c` | `process_edit()` vérifiait "POST sur ressource existante -> 409" (RFC 8040 Sec 4.4) **avant** tout parsing du corps de la requête : un JSON malformé posté sur une ressource déjà existante (ex. `restconf-test:system`, peuplé par des tests antérieurs) renvoyait donc 409 au lieu du 400 attendu par RFC 8040 Sec 7.1 (une requête malformée doit être rejetée avant toute logique métier). **Fix** : le contrôle d'existence 409 est déplacé après le parsing réussi du corps (juste avant `sr_edit_batch()`) ; un corps malformé tombe désormais dans le chemin d'erreur de parsing standard (400) avant même d'atteindre ce contrôle. Comportement 409 pour un POST valide sur ressource existante inchangé (cf. `test_crud.py::test_012_post_existing_resource`). **À confirmer par `./scripts/build_test.sh`.** |
| Souscription/receiver individuel | 6.1 | `sysrepo_plugin.c`, `main.c` | Un seul flux `NETCONF` diffuse tout ; pas de `xpath-filter` par souscription |
| Replay mode Externe | 6.5.1 | `uds_gateway.c` | Stub `NULL` ; fallback live-only propre |
| Hot-reload contexte libyang externe | 3.10.1 | `uds_gateway.c` | Contexte figé au démarrage du gateway |
| Limites ressources non configurables | 7.3.1 | `h2c_server.c` | `H2C_MAX_REQUEST_BODY_SIZE`/`MAX_CONCURRENT_STREAMS` en `#define` ; pas de limite par IP/utilisateur |
| Tests conformité `nghttp` manuels | 7.4 | `test/` | - |

---

## 🎯 Prochaines Étapes (par priorité)

1. **Confirmer le fix `test_008_put_modify_existing`** — lancer
   `./scripts/build_test.sh` pour valider que le passage à
   `sr_edit_batch()` (cf. journal ci-dessus) fait bien passer ce test
   à 201/204/404 comme attendu, sans régression sur `test_006`/`test_007`
   ni les autres scénarios CRUD.
2. **8.5** — Confirmer par exécution réelle (`ctest
   -DBUILD_CTEST_INTEGRATION=ON`) les scénarios d'erreur revus par
   lecture de code seule.
3. **6.1 (suivi)** — `xpath-filter` RFC 8650 par souscription +
   corrélation ID↔flux SSE HTTP/2 (notion de "receiver").
4. **6.5.1 (suivi)** — Replay en mode Externe (message IPC dédié) +
   vérification automatique du `replay-support` sysrepo par module.
5. **7.3.1 (suivi)** — Rendre configurables les limites HTTP/2
   (actuellement `#define`) ; limite par IP/utilisateur si besoin.
6. **3.10.1 (suivi)** — Hot-reload du contexte libyang local du
   gateway externe si le redémarrage systématique s'avère gênant.

---

## 📋 Roadmap des Tests

Feuille de route détaillée des tests de conformité RESTCONF :
- **TEST-ROADMAP.md** (racine du dépôt)
- **doc/test/TEST-ROADMAP.md** (version détaillée avec statistiques)
