# restconfd — RESTCONF Server (RFC 8040 + RFC 8527)

Serveur léger d'implémentation de **RESTCONF** basés sur les RFC IETF 8040 et 8527, s'appuyant sur:

- **[fcgi2](https://github.com/FastCGI-Archives/fcgi2)** — Transport HTTP via FastCGI (API `FCGX_*`), exposé par un serveur web frontal (nginx)
- **[sysrepo](https://github.com/sysrepo/sysrepo)** — Moteur de datastores YANG/NMDA, API C et libyang pour la sérialisation JSON (RFC 7951)
- **[yang](https://github.com/YangModels/yang.git)** — Modules standards-track YANG IETF
- **[libev](https://dist.schmorp.de/libev/libev-4.33.tar.gz)** — Event loop haute performance

---

## Références

| Document | Description | URL |
|----------|-------------|-----|
| RFC 8040 | RESTCONF Protocol (YANG Data Models) | https://datatracker.ietf.org/doc/html/rfc8040 |
| RFC 8527 | YANG Datastores (running/candidate/startup/operational) | https://datatracker.ietf.org/doc/html/rfc8527 |
| sysrepo | IANA YANG datastore library | https://github.com/sysrepo/sysrepo |
| fcgi2 | FastCGI application proxy | https://github.com/FastCGI-Archives/fcgi2 |

---

## État d'Implémentation

### Références RFC et Fonctions

| Fonctionnalité RFC | Statut | Détails |
|--------------------|--------|---------|
| **Discovery** | ✅ Done | `{.well-known/host-meta}`, `OPTIONS`, `Allow` header |
| **Datastore Access** | ✅ Done | GET/HEAD sur `/{+restconf}` et `/{+restconf}/ds/<name>/<path>` |
| | | POST pour création (running/candidate/startup), pas sur operational |
| | | PUT pour remplacement complet, y compris racine de datastore |
| | | PATCH fusion (plain patch), y compris racine de datastore |
| | | DELETE sur ressources de données, non défini sur racine de datastore |
| **Content Negotiation** | ✅ Done | Accept → `application/yang-data+json` uniquement ; rejet explicite `406 Not Acceptable` pour XML |
| **Query Parameters** | ✅ Done | `depth`, `fields`, `with-defaults`, `with-origin` (support complet) |
| | | `insert`, `point`, `filter`, `start-time`, `stop-time` → rejet explicite `400 invalid-value` |
| **Content Parameter** | Partiel | `config\|\|nonconfig` filtre seulement sur operational ; ignoré pour running/candidate/startup |
| **Conditionals** | ✅ Done | `ETag`, `Last-Modified`, `If-Match`, `If-Unmodified-Since` au niveau du datastore entier |
| **RPC Operations** | ✅ Done | POST sur `/{+restconf}/operations/<module>:<rpc>` → `sr_rpc_send_tree()` avec recherche LYS_OUTPUT |
| **Actions YANG** | ✅ Done | Invocation sous `/{+restconf}/ds/ietf-datastores:operational` via `sysrepo_backend_is_action_path()`, rejet ailleurs |
| **Notifications SSE** | ✅ Done (v1) | GET `/{+restconf}/streams/<module>` avec `Accept: text/event-stream` → `sr_notif_subscribe_tree()` ; temps réel uniquement, JSON uniquement, un thread FastCGI par flux ouvert (cf. Phase 2 pour les limites restantes) |

### Fonctionnalités Non Implémentées

| Fonctionnalité | Statut | Notes |
|----------------|--------|-------|
| Notifications SSE (Server-Sent Events) | ✅ Done (v1) | `{+restconf}/streams/<module>` + `sr_notif_subscribe_tree()` ; un flux = un module YANG ; pas de rejeu (`start-time`/`stop-time` non supportes) ; discovery via `restconf-state/streams` (plugin) |
| XML Full Support | ⬜ Not Started | Media type negotiation, YANG XML serializer (`lyd_print_mem`), RESTCONF ops XML |
| Authentication/NACM | ⬜ Planned | Framework TLS + NACM (`sr_session_set_user`, `SR_SESS_ENABLE_NACM`), JWT handling (RFC7519/8725/7797) |

---

## Procédure de Construction

### Étape 1 : Prérequis

```bash
# Outils système
apt update && apt install -y build-essential cmake gcc make m4 autoconf automake libtool libssl-dev libjson-c-dev libpcre2-dev nginx

# Systèmes dependencies
sudo apt install -y sysrepo libsysrepo-dev libyang-dev fcgi-app
```

### Étape 2 : Build avec CMake

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
make
sudo make install
```

**Options de configuration CMake :**

| Option | Valeur par défaut | Description |
|--------|-------------------|-------------|
| `BUILD_TESTS` | OFF | Compiler les tests unitaires (réservé pour plus tard) |
| `INSTALL_RUNTIME_LIBS` | OFF | Copier les `.so` de sysrepo/libyang/fcgi2 dans le répertoire d'installation |

**Exemple complet :**
```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug \
-DBUILD_TESTS=ON \
-DINSTALL_RUNTIME_LIBS=ON
```

### Étape 3 : Installation des modules YANG

Après compilation, installez les modules IETF requis :
```bash
sudo sysrepoctl -i /path/to/restconf/yang/ietf-restconf@2017-01-26.yang
sudo sysrepoctl -i /path/to/restconf/yang/ietf-restconf-monitoring@2017-01-26.yang
sudo sysrepoctl -i /path/to/restconf/yang/ietf-restconf-subscribed-notifications@2019-11-17.yang
sudo sysrepoctl -i /path/to/restconf/yang/ietf-yang-library@2022-11-04.yang
sudo sysrepoctl -i /path/to/restconf/yang/ietf-datastores@2022-05-01.yang
sudo sysrepoctl -i /path/to/restconf/yang/ietf-netconf@2019-06-29.yang
```

### Étape 4 : Lancement avec Docker

```bash
docker build -f docker/Dockerfile -t restconfd:latest .
docker run --rm -p 80:80 restconfd
```

**Fichier `Dockerfile` — base image :** `debian:bookworm-slim`  
**Dépendances compilées dans l'image :** fcgi2, sysrepo, libyang, pthreads  
**Commande de démarrage :** `/restconf/docker/start-nginx.sh` → nginx + restconfd sur port 9000

---

## Description des Fonctions et Choix Architecturaux

### Mapping Datastore RESTCONF ↔ sysrepo

| RESTCONF Endpoint | sysrepo Datastore | Rationale |
|--------------------|-------------------|-----------|
| `/{+restconf}` | `SR_DS_RUNNING` | Configuration uniquement ; point d'écriture standard RFC 8040 |
| `/{+restconf}/ds/ietf-datastores:running` | `SR_DS_RUNNING` | Correspondance directe via datastore naming |
| `/{+restconf}/ds/ietf-datastores:candidate` | `SR_DS_CANDIDATE` | Travaux en cours de validation avec sysrepo |
| `/{+restconf}/ds/ietf-datastores:startup` | `SR_DS_STARTUP` | Configuration persistante pour démarrage |
| `/{+restconf}/ds/ietf-datastores:operational` | `SR_DS_OPERATIONAL` | **ROULE SEULE** ; sysrepo peuple ce datastore via `sr_oper_get_items_subscribe()`/push oper data, pas via PUT/PATCH RESTCONF classiques |

**Choix design :** Cette séparation suit strictement la RFC 8527 — les datastores opérationnels sont accessibles mais non écrivables depuis RESTCONF pour éviter des mutations non contrôlées. À revoir si besoin de pousser des données operationnelles via RESTCONF.

### Paramètres de Requête

| Paramètre | Support | Implémentation |
|-----------|---------|----------------|
| `depth` | ✅ Oui | Entier positif ou `unbounded` ; utilise `sr_get_data(..., max_depth, ...)` |
| `fields` | ✅ Oui | Grammaire complète de champs |
| `with-defaults` | ✅ Oui | Les 4 valeurs RFC 8040 SS4.8.9 : all, default, default-with-origin, non-default |
| `with-origin` | ✅ Oui (limited) | Seulement sur `{+restconf}/ds/ietf-datastores:operational` per RFC 8527 SS3.2.2 |

**Rejet explicite :** Les paramètres `insert`, `point`, `filter`, `start-time`, `stop-time` sont rejetés avec `400 invalid-value` plutôt que ignorés silencieusement.

### RPC et Actions YANG

| Type | Endpoint autorisé | Méthode autorisée |
|------|-------------------|-------------------|
| **RPC Operations** | `/{+restconf}/operations/<module>:<rpc>` | POST uniquement via `sr_rpc_send_tree()` avec recherche de LYS_OUTPUT |
| **Actions YANG** | `/{+restconf}/ds/ietf-datastores:operational`/... | POST uniquement via `sysrepo_backend_is_action_path()` et `sysrepo_backend_action_invoke()` ; rejet `400 invalid-value` ailleurs per RFC 8527 SS3.1 |

### ETag / Conditionals

| Feature | Implémentation |
|---------|----------------|
| `ETag`, `Last-Modified` (Response headers) | ✅ Oui, au niveau du datastore entier |
| `If-Match`, `If-Unmodified-Since` (Request headers) | ✅Oui pour préconditions ; rejet `412 Precondition Failed` si collision |

### Paramètre `content=config\|\|nonconfig`

**Comportement actuel :**
- Filtre actif seulement sur le datastore `operational` via les indicateurs `SR_OPER_NO_STATE`/`SR_OPER_NO_CONFIG` (vérifier ces noms de constantes dans `sysrepo.h` selon votre version)
- Ignoré pour running/candidate/startup (purement configuration chez sysrepo)

---

## Feuille de Route
### Analyse de Cohérence Implémentation vs RFC

| RFC | Score de cohérence | Incohérences critiques |
|-----|--------------------|------------------------|
| **RFC 8040** | ✅ ~95% | Mineures (SS3.6, SS4.5) |
| **RFC 8527** | ⚠️ ~70-80% | Actions Yang restriction, with-origin, ETag granulaire |

---

## Détails d'Incohérences par RFC

### 🔴 **RFC 8040 : Points d'attention potentiels**

| Zone RFC | Observation | Impact |
|----------|-------------|--------|
| **SS3.6 (Operations)** | Actions invokées sous `/data/<...>/<action>` vs ops dans `/operations` | Note explicite en code, architecture sysrepo ne sépare pas bien les deux |
| **SS4.5 (Creation)** | POST sur root datastore → `201 Created` requis | Implémentation peut envoyer `204 No Content` selon le cas |

### 🔴 **RFC 8527 : NMDA Extensions — Incohérences identifiées**

| RFC 8527 Section | Exigence | État implémentation |
|------------------|----------|--------------------|
| **SS3.1** | Actions YANG *uniquement* sur `/ds/ietf-datastores:operational` | ⚠️ Partiel : code dit "n'autorise donc pas..." mais actions restent invocables ailleurs |
| **SS3.2.1** | `"with-defaults"` doit reporter les par défaut de *chaque* datastore (trim,report-all,master) | ✅ Implémentation basique |
| **SS3.2.2** | `"with-origin"` doit suivre le chemin d'origine du tree (`sr_get_oper_flag_t`) | ✅ Implémentée mais pas de validation stricte du flag sysrepo |
| **SS3.4.1-3.4.1.2** | ETag/Last-Modified au niveau de la *ressource*, pas seulement du datastore entier | ❌ Pas implémentée : valeurs partagées entre toutes les ressources |

---

### 🔴 **Points d'incohérence critiques (RFC 8527)**

#### 1. **Actions YANG hors operational**
- **RFC exige** : Actions uniquement invocabiles sous `{+restconf}/ds/ietf-datastores:operational`
- **Code actuel** : Commentaire dit "n'autorise donc pas...", mais `sr_backend_is_action_path()` n'est *pas* vérifié avant les ops

#### 2. **with-origin selon datastore**
- **RFC exige** : Pour `/running`, le chemin d'origine diffère de `/operational` (car running = config)
- **Code actuel** : `with-origin` force systématiquement `/operational` (lignes ~519-520 de sysrepo_backend.c)

#### 3. **ETag par ressource vs datastore**
- **RFC exige** : ETag unique par ressource individuelle, pas seulement par datastore global
- **Code actuel** : Même ETag utilisé pour toutes les ressources d'un même datastore

---

### ✅ **RFC 8040 / RFC 8527 : Implémentation correcte**

| Fonctionnalité | Statut |
|----------------|--------|
| Root resource discovery | ✅ Correct |
| Datastore CRUD sur `/ds/ietf-datastores:<name>` | ✅ Correct |
| Query params: depth, fields, with-defaults, with-origin | ✅ Correct |
| Yang Library Version resource | ✅ Correct |
| Operations resource (POST / <module>:<rpc>) | ✅ Correct |
| Error mapping table (RFC 8040 SS7) | ✅ Correct |

---

## Priorités de Correction RFC 8527

| Priorité | Point à corriger | Effort estimé |
|----------|------------------|---------------|
| 🔴 Critical | Actions Yang restriction stricte (vérifier `sr_backend_is_action_path()` avant tout op) | 1-2 jours |
| 🟡 High | with-origin behaviour selon datastore (running vs operational) | 1 jour |
| 🟡 High | ETag granulaire par ressource (pas seulement par datastore global) | 2-3 jours |

### Phase 1: Core RESTCONF (✅ COMPLETED)

| # | Tâche | Effort | Notes |
|---|-------|--------|-------|
| 1 | Implémentation GET/HEAD sur ressources RESTCONF | ✅ Done | Support RFC 8040 SS4.x pour les endpoints de base |
| 2 | Implémentation CRUD (POST/PUT/PATCH/DELETE) | ✅ Done | Support complet avec gestion des erreurs selon RFC 8040 |
| 3 | Intégration sysrepo backend (get_set_tree, sr_rpc_send_tree, actions) | ✅ Done | Mappage correct des chemins YANG vers datastores |
| 4 | Support de paramètres RESTCONF (depth/fields/with-defaults/with-origin) | ✅ Done | Validation et filtrage selon RFC 8040 |
| 5 | Intégration fcgi2 pour transport HTTP | ✅ Done | Utiliser API FCGX_* via backend http_catch |
| 6 | Configuration nginx + FastCGI | ✅ Done | Split path de `/{+restconf}` en SCRIPT_NAME/PATH_INFO |

**Total Phase 1:** ~0 jour (complet)

### Phase 2: Notifications SSE (🟡 EN COURS — v1 fonctionnelle)

| # | Tâche | Effort | Status | Notes |
|---|-------|--------|--------|-------|
| 7 | Implémentation `sr_notif_subscribe_tree()` | ✅ Done | ~1 jour | `sysrepo_backend_stream_subscribe()`/`_unsubscribe()` (src/sysrepo_backend.c) : session sysrepo dédiée par flux, callback `stream_notif_cb()` convertit chaque notification en JSON enveloppe RFC 8040 SS6.4 |
| 7bis | Endpoint HTTP `{+restconf}/streams/<module>` + boucle SSE | ✅ Done | ~1 jour | `handle_streams()` (src/restconf_handler.c) : file FIFO mutex+condvar entre le thread sysrepo (callback) et le thread de requête FastCGI (écriture SSE), heartbeat 15s (`http_sse_send_comment()`) pour détecter une déconnexion client |
| 7ter | Discovery `restconf-state/streams` | ✅ Done | ~0.5 jour | Plugin `restconf_monitoring` (`restconf_monitoring_streams_cb()`) : un flux par module YANG implementé portant des notifications ; racine `{+restconf}` supposée fixe (`/restconf`, cf. limite documentée dans le plugin) |
| 8 | Gestion du buffering pour haut volume | 🟡 Pending | 1-2 jours | La file FIFO actuelle n'est pas bornée (croissance illimitée si le client lit plus lentement que les notifications n'arrivent) ; tests de charge et ajustement mémoire à faire |
| 9 | Conversion JSON ↔ XML notifications | 🔵 Pending | 1-2 jours | Dépend de la Phase 3 (XML Full Support) ; `handle_streams()` n'accepte pour l'instant que `Accept: text/event-stream` avec charge JSON |
| 10 | Rejeu ("start-time"/"stop-time") | ⬜ Pending | 2-3 jours | `sr_notif_subscribe_tree()` est appelé avec `start_time=stop_time=0` (temps réel uniquement) ; brancher les query parameters RFC 8040 SS4.8.7/4.8.8 (actuellement rejetés partout ailleurs dans ce serveur) demanderait de revoir ce choix |
| 11 | Un seul thread FastCGI par flux SSE ouvert | ⚠️ Limite connue | - | Ce squelette garde un thread worker FastCGI bloqué par connexion SSE (pas de libev malgré la dépendance listée en tête de ce fichier) : le nombre de flux concurrents est donc plafonné par `g_nthreads` (argv[2] de restconfd, défaut 4) moins les threads nécessaires aux requêtes RESTCONF classiques -- à revoir (event loop dédiée) avant un usage en production avec beaucoup de clients SSE simultanés |

**Total Phase 2 restant:** ~5-7 jours estimé (buffering, rejeu, event loop dédiée ; XML reporté en Phase 3)

### Phase 3: XML Full Support (⬜ PENDING)

| # | Tâche | Effort | Status | Notes |
|---|-------|--------|--------|-------|
| 11 | Media type negotiation (`Accept` parsing) | ⬜ Pending | 1-2 jours | Gestion `application/yang-data+json` vs `+xml`, refus propre `406 Not Acceptable` |
| 12 | YANG XML serializer (`lyd_print_mem`) | ⬜ Pending | 2-3 jours | Implémentation analogue à `sysrepo_backend_get()` pour JSON, XML native |
| 13 | RESTCONF ops support avec content negotiation | ⬜ Pending | 2-3 jours | POST/PUT/PATCH XML; handling `with-origin` via sysrepo SSE notifications |

**Total Phase 3:** ~6 jours estimé

### Phase 4: Advanced Features (⬜ PENDING)

| # | Tâche | Effort | Status | Notes |
|---|-------|--------|--------|-------|
| 14 | NACM full implementation | ⬜ Pending | 6-10 jours | Étude `sr_session_set_user`, `SR_SESS_ENABLE_NACM`; JWT handling framework (RFC7519/8725/7797); keyring kernel integration |
| 15 | Advanced error handling (RFC 8040 SS7-8) | ⬜ Pending | 3-5 jours | Error tags spécifiques par type d'opération ; mapping systématique sysrepo → RESTCONF errors |

**Total Phase 4:** ~10 jours estimé

### Phase 5: Formal Verification (⬜ PENDING)

| # | Tâche | Effort | Status | Notes |
|---|-------|--------|--------|-------|
| 16 | Frama-C code instrumentation | ⬜ Pending | 4-6 jours | Formal verification avec instrumentations ; property proofs pour sécurité et correctionnelle |
| 17 | AFL++ fuzzing de la library core | ⬜ Pending | 3-5 jours | Fuzz testing pour validation des entrées malveillantes et débordements |
| 18 | Stress testing sous 10k+ req/s | ⬜ Pending | 2-4 jours | Tests de charge ; validation de robustesse et scalabilité |

**Total Phase 5:** ~13 jours estimé

### Phase 6: Documentation & Testing (⬜ PENDING)

| # | Tâche | Effort | Status | Notes |
|---|-------|--------|--------|-------|
| 19 | Sample applications (oven style sysrepo) | ⬜ Pending | 2-3 jours | Modèles d'utilisation ; exemples GET/POST/PUT/PATCH/DELETE/RPC/actions |
| 20 | API documentation (Doxygen + man pages) | ⬜ Pending | 1-2 jours | Docs utilisateurs complets avec examples et scenarios |
| 21 | Integration test suite | ⬜ Pending | 3-5 jours | Test complet de tous les endpoints RESTCONF ; regression testing et CI/CD integration |

**Total Phase 6:** ~7 jours estimé

---

## Synthèse Totale

| Phase | Effort estimé | Priorité | Status |
|-------|---------------|----------|--------|
| Phase 1: Core | ✅ 0 jour | 🔴 Top Priority | **Completed** |
| Phase 2: SSE | ~5-7 jours restants | 🟡 High | **v1 fonctionnelle** (flux temps réel JSON, discovery) ; durcissement restant (buffering, rejeu, event loop dédiée) |
| Phase 3: XML | ~6 jours | 🟡 Medium | ⬜ Pending |
| Phase 4: Advanced | ~10 jours | 🔴 Critical | ⬜ Pending |
| Phase 5: Verification | ~13 jours | 🟢 Low | ⬜ Pending |
| Phase 6: Documentation | ~7 jours | 🟡 Medium | ⬜ Pending |

**Total estimé restant:** **~41-43 jours** pour completion complète du serveur RESTCONF (contre 36-44 jours avant la Phase 2 v1 ; le total global n'a pas beaucoup baisse car la Phase 2 n'est fonctionnelle qu'en v1, cf. items 8/9/10/11 encore ouverts).

---

## Actions Prioritaires (Next Sprint)

1. **Priorité 1 🟡**: Durcir les notifications SSE : file bornée (item 8), event loop dédiée au lieu d'un thread FastCGI par flux (item 11) -- la v1 fonctionnelle (`sr_notif_subscribe_tree()` + `{+restconf}/streams/<module>`) est en place
2. **Priorité 2 🟡**: Auth/NACM framework integration
3. **Priorité 3 🟢**: Sample applications + documentation utilisateur