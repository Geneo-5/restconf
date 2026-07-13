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
| **8** | Tests h2c & Intégration CTest | 95% | 🟡 |

*(⚪ À faire | 🟡 En cours | 🟢 Terminé | 🔴 Bloquant ; `[x]` fait,
`[~]` partiel, `[ ]` à faire)*

**État des tests (dernier run connu, 145 tests) : 143/145 (98.6%)
attendu, à confirmer.** 2 échecs ouverts, cf. « Dette technique ».

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
| 8.5 | Tests gestion erreurs | `[~]` | Revue de code seule (3 scénarios) ; à confirmer par exécution réelle |

---

## ⚠️ Dette Technique Ouverte

| Sujet | Item(s) | Fichier(s) | État |
| :--- | :---: | :--- | :--- |
| **2 tests en échec** | 8.5 | `test/test_crud.py` | `test_008_put_modify_existing` (400 au lieu de 201/204/404) : PUT sur entrée de liste, cause non identifiée malgré relecture (`codec_parse_data()` semble structurellement identique au chemin POST déjà validé) — logger `RC_ERROR` déjà en place, lancer `build_test.sh --verbose` en priorité. `test_015_action_no_params` (415) : bloqué par les actions YANG 1.1 commentées dans `restconf-test.yang` (problème libyang 5.8.6, pas un bug serveur) |
| Souscription/receiver individuel | 6.1 | `sysrepo_plugin.c`, `main.c` | Un seul flux `NETCONF` diffuse tout ; pas de `xpath-filter` par souscription |
| Replay mode Externe | 6.5.1 | `uds_gateway.c` | Stub `NULL` ; fallback live-only propre |
| Hot-reload contexte libyang externe | 3.10.1 | `uds_gateway.c` | Contexte figé au démarrage du gateway |
| Limites ressources non configurables | 7.3.1 | `h2c_server.c` | `H2C_MAX_REQUEST_BODY_SIZE`/`MAX_CONCURRENT_STREAMS` en `#define` ; pas de limite par IP/utilisateur |
| Tests conformité `nghttp` manuels | 7.4 | `test/` | - |

---

## 🎯 Prochaines Étapes (par priorité)

1. **Diagnostiquer `test_008_put_modify_existing`** — nécessite une
   exécution réelle de `./scripts/build_test.sh` (résultat dans build.log);
   le log `RC_ERROR` de `codec_parse_data()` en cas d'échec libyang
   est le point d'entrée le plus rapide.
2. **8.5** — Confirmer par exécution réelle (`ctest
   -DBUILD_CTEST_INTEGRATION=ON`) les 3 scénarios d'erreur revus par
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
