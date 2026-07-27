# Roadmap - Support du protocole RESTCONF

Ce document liste les étapes d'implémentation nécessaires pour supporter le protocole RESTCONF et ses extensions, avec leurs dépendances et leur avancement.

| ID | RFC + § | Description | Dépendances | Avancement |
|----|---------|--------------|--------------|------------|
| R1 | [RFC 8040 §2](https://datatracker.ietf.org/doc/html/rfc8040#section-2) - Transport | Backend en h2c (HTTP/2 cleartext) derrière un reverse proxy qui termine le TLS ; la RFC 8040 exige HTTPS côté client, ce qui est satisfait au niveau du proxy — le backend n'a pas besoin de gérer TLS lui-même | — | ⬜ À faire |
| R2 | [RFC 8040 §2](https://datatracker.ietf.org/doc/html/rfc8040#section-2) - Identification du client | Le backend **valide lui-même l'authenticité du JWT** (vérification de signature via JWKS/clé publique, `exp`, `iss`, `aud`, etc.) reçu dans le header `Authorization: Bearer`, puis en extrait les claims pour identifier l'utilisateur ; l'émission du JWT reste déléguée à un service d'auth externe, mais la vérification cryptographique est faite côté backend | R1 | ⬜ À faire |
| R3 | [RFC 8040 §3.1](https://datatracker.ietf.org/doc/html/rfc8040#section-3.1) - Root Resource Discovery | Découverte de la racine `{+restconf}` via `/.well-known/host-meta`. Endpoint **non authentifié** (contenu public, non sensible) : à exclure de la validation JWT côté reverse proxy/backend | R1 | ⬜ À faire |
| R4 | [RFC 8040 §3.3](https://datatracker.ietf.org/doc/html/rfc8040#section-3.3) - API Resource | Exposer la ressource racine `yang-api` (liste des sous-ressources data/operations/streams) | R3 | ⬜ À faire |
| R5 | [RFC 8040 §3.4](https://datatracker.ietf.org/doc/html/rfc8040#section-3.4) - Datastore Resource | Exposer la ressource de datastore (vue unifiée du datastore conceptuel) | R4 | ⬜ À faire |
| R6 | [RFC 8040 §4.3](https://datatracker.ietf.org/doc/html/rfc8040#section-4.3) - Data Resource : GET | Lecture d'une ressource de données (nœud config ou state), y compris listes/leaf-list, gestion des en-têtes `ETag`/`Last-Modified` | R5 | ⬜ À faire |
| R7 | [RFC 8040 §4.4](https://datatracker.ietf.org/doc/html/rfc8040#section-4.4) - Data Resource : POST | Création d'une ressource enfant (POST sur le parent) et invocation d'actions YANG (POST sur une ressource de données) | R5 | ⬜ À faire |
| R8 | [RFC 8040 §4.5](https://datatracker.ietf.org/doc/html/rfc8040#section-4.5) - Data Resource : PUT | Création ou remplacement complet d'une ressource de données à un chemin donné | R5 | ⬜ À faire |
| R9 | [RFC 8040 §4.7](https://datatracker.ietf.org/doc/html/rfc8040#section-4.7) - Data Resource : DELETE | Suppression d'une ressource de données existante | R5 | ⬜ À faire |
| R10 | [RFC 8040 §4.6](https://datatracker.ietf.org/doc/html/rfc8040#section-4.6) - PATCH ("plain patch") | Support du PATCH partiel simple sur une ressource de données | R6, R8 | ⬜ À faire |
| R11 | [RFC 8040 §3.6](https://datatracker.ietf.org/doc/html/rfc8040#section-3.6) - Operations Resource | Invocation des RPC/actions YANG via POST sur `/operations` | R4 | ⬜ À faire |
| R12 | [RFC 8040 §8](https://datatracker.ietf.org/doc/html/rfc8040#section-8) / [RFC 8525](https://datatracker.ietf.org/doc/html/rfc8525) - YANG Library | Exposer l'inventaire des modules YANG supportés (`ietf-yang-library`), utilisé pour construire les liens vers les ressources de schéma | R5 | ⬜ À faire |
| R13 | [RFC 8040 §3.7](https://datatracker.ietf.org/doc/html/rfc8040#section-3.7) - Schema Resource (GET) | Endpoint GET servant le code source d'un module YANG (`application/yang`), référencé par la leaf `location` de la yang-library, exposé sur `{+restconf}/yang/{module}@{revision}.yang`. **Implémentation sysrepo (C)** : le leaf-list `location` n'est pas rempli nativement par sysrepo (il ignore l'URL du serveur RESTCONF) — s'abonner comme provider de données opérationnelles via `sr_oper_get_items_subscribe()` sur `/ietf-yang-library:yang-library/module-set[name='complete']/module/location` (+ `import-only-module/location` et les variantes `submodule/location`), callback qui itère le contexte libyang (`ly_ctx_get_module_iter()`) et construit dynamiquement `{+restconf}/yang/<name>@<revision>.yang` pour chaque module via `lyd_new_path()` | R12 | ⬜ À faire |
| R14 | [RFC 8040 §3.7](https://datatracker.ietf.org/doc/html/rfc8040#section-3.7) - `get-schema` (optionnel) | Alternative via l'opération RPC `get-schema` (module `ietf-netconf-monitoring`) exposée sur `/operations`, pour les modules sans URL de `location` statique | R11 | ⬜ À faire |
| R15 | [RFC 8040 §4.8.1-4.8.3, §4.8.9](https://datatracker.ietf.org/doc/html/rfc8040#section-4.8.1) - Query Params : mise en forme du GET | Paramètres `content` (config/state), `depth` (profondeur), `fields` (sous-ensemble), `with-defaults` (gestion des valeurs par défaut) | R6 | ⬜ À faire |
| R16 | [RFC 8040 §4.8.5, §4.8.6](https://datatracker.ietf.org/doc/html/rfc8040#section-4.8.5) - Query Params : ordonnancement | Paramètres `insert` et `point` pour la création dans des listes/leaf-lists `ordered-by user` | R7, R8 | ⬜ À faire |
| R17 | [RFC 8040 §4.8.4, §4.8.7, §4.8.8](https://datatracker.ietf.org/doc/html/rfc8040#section-4.8.4) - Query Params : flux d'événements | Paramètres `filter` (filtrage booléen des notifications), `start-time`/`stop-time` (replay) pour les ressources de type "stream" | R19 | ⬜ À faire |
| R18 | [RFC 8040 §7](https://datatracker.ietf.org/doc/html/rfc8040#section-7) - Error Reporting | Modèle d'erreurs "errors" YANG data template dans les réponses | R6, R7, R8, R9 | ⬜ À faire |
| R19 | [RFC 8040 §3.8 / §6](https://datatracker.ietf.org/doc/html/rfc8040#section-6) - Event Stream Resource (SSE) | Flux d'événements via Server-Sent Events (long-poll GET sur une ressource "stream") | R6, R13 | ⬜ À faire |
| R20 | [RFC 8072](https://datatracker.ietf.org/doc/html/rfc8072) - YANG Patch Media Type | Édition de plusieurs sous-ressources en une seule requête PATCH (`application/yang-patch+xml/json`) | R10 | ⬜ À faire |
| R21 | [RFC 8527](https://datatracker.ietf.org/doc/html/rfc8527) - RESTCONF NMDA Extensions | Support des ressources de datastore multiples (`running`, `operational`, etc.) et paramètre `with-origin` | R5, R6 | ⬜ À faire |
| R22 | [RFC 8341](https://datatracker.ietf.org/doc/html/rfc8341) - NETCONF/RESTCONF Access Control Model | Contrôle d'accès (NACM) appliqué aux opérations et contenus RESTCONF, basé sur les claims du JWT (rôles/groupes) au lieu du username/password NETCONF classique | R2 | ⬜ À faire |
| R23 | [RFC 8071](https://datatracker.ietf.org/doc/html/rfc8071) - RESTCONF Call Home | Connexion sortante initiée par le serveur (call home) vers le client de gestion | R1 | ⬜ À faire |
| R24 | [RFC 8639 §2.4.1](https://datatracker.ietf.org/doc/html/rfc8639#section-2.4.1) - `establish-subscription` | RPC de création d'une souscription (générique, indépendant du transport) et modèle d'état `/subscriptions` | R11 | ⬜ À faire |
| R25 | [RFC 8639 §2.4.2-2.4.4](https://datatracker.ietf.org/doc/html/rfc8639#section-2.4.2) - `modify` / `delete` / `kill-subscription` | RPC de gestion du cycle de vie d'une souscription existante | R24 | ⬜ À faire |
| R26 | [RFC 8639 §2.4.5-2.4.6](https://datatracker.ietf.org/doc/html/rfc8639#section-2.4.5) - Notifications de cycle de vie | Notifications `subscription-terminated`, `subscription-modified`, `subscription-resumed`/`-suspended` envoyées au souscripteur | R24 | ⬜ À faire |
| R27 | [RFC 8641 §2.1](https://datatracker.ietf.org/doc/html/rfc8641#section-2.1) - YANG-Push : mode "on-change" | Souscription déclenchée par changement dans le datastore, avec `dampening-period` | R24, R21 | ⬜ À faire |
| R28 | [RFC 8641 §2.2](https://datatracker.ietf.org/doc/html/rfc8641#section-2.2) - YANG-Push : mode périodique | Souscription à échantillonnage périodique (`periodic`/`period`) | R24, R21 | ⬜ À faire |
| R29 | [RFC 8641 §3.6](https://datatracker.ietf.org/doc/html/rfc8641#section-3.6) - `resync-subscription` | RPC permettant de forcer un renvoi complet de l'état courant (resynchronisation) | R27, R28 | ⬜ À faire |
| R30 | [RFC 8650 §2.1-2.2](https://datatracker.ietf.org/doc/html/rfc8650#section-2.1) - Binding RESTCONF : établissement | `establish-subscription` invoqué via POST sur `/operations`, retour d'une `uri` dédiée pour la souscription | R24, R11 | ⬜ À faire |
| R31 | [RFC 8650 §2.3](https://datatracker.ietf.org/doc/html/rfc8650#section-2.3) - Binding RESTCONF : gestion | `modify`/`delete`/`kill-subscription` via POST sur `/operations`, cohérent avec R25 | R25, R30 | ⬜ À faire |
| R32 | [RFC 8650 §3](https://datatracker.ietf.org/doc/html/rfc8650#section-3) - Binding RESTCONF : réception | GET long-poll (SSE) sur la `uri` de souscription pour recevoir les notifications poussées (YANG-Push ou événements) | R30, R19 | ⬜ À faire |

## Légende avancement
- ⬜ À faire
- 🟡 En cours
- ✅ Terminé
- ⛔ Bloqué

## Contexte de déploiement
- Le serveur RESTCONF est un backend en **h2c** (HTTP/2 en clair), placé derrière un **reverse proxy** qui gère la terminaison TLS/HTTPS. Vis-à-vis du client final, le protocole reste bien conforme à l'exigence HTTPS de la RFC 8040.
- L'**émission** du JWT est déléguée à un service tiers en amont (service d'auth), mais c'est le **backend RESTCONF qui valide lui-même l'authenticité du token** : vérification de la signature (via JWKS ou clé publique partagée), des claims standards (`exp`, `iss`, `aud`, etc.), avant d'en extraire l'identité pour l'autorisation (mapping NACM).
- Point de vigilance : s'assurer que le reverse proxy est bien la seule voie d'accès au backend h2c (réseau interne non exposé), pour éviter que la validation JWT ne soit contournable via un accès direct.

## Points d'attention techniques (stack libevent + libnghttp2 + libsysrepo + libjwt)

| ID | Composant | Point d'attention | R lié | Avancement |
|----|-----------|--------------------|-------|------------|
| A1 | libnghttp2 | Lib bas niveau (pas nghttp2-asio) : câbler soi-même `send_callback`/`recv_callback` sur des `bufferevent` libevent, parser les pseudo-headers (`:method`, `:path`, `:authority`, `:scheme` — pas de header `Host` en HTTP/2), assembler HEADERS+DATA en requête exploitable | R1 | ⬜ À faire |
| A2 | libnghttp2 / reverse proxy | Le proxy doit parler h2c en **prior knowledge** vers le backend (pas d'Upgrade HTTP/1.1) — ex. nghttpx `-b '::1,10080;...;proto=h2'` sans TLS côté backend | R1 | ⬜ À faire |
| A3 | libnghttp2 | Flow control HTTP/2 sur les flux SSE : chaque DATA frame doit respecter la fenêtre de crédit du stream (`nghttp2_session_get_stream_remote_window_size`), sinon blocage silencieux sous forte volumétrie ; relancer l'envoi via `nghttp2_session_resume_data()` sur WINDOW_UPDATE | R19, R32 | ⬜ À faire |
| A4 | libnghttp2 | Pas de support SSE natif : encoder soi-même les lignes `data: ...\n\n` dans les DATA frames et positionner `Content-Type: text/event-stream` | R19, R32 | ⬜ À faire |
| A5 | libevent / libsysrepo | Les appels `sr_get_data`/`sr_edit_batch`/`sr_apply_changes`/`sr_rpc_send` sont **synchrones/bloquants** (IPC vers le démon sysrepo) — à ne pas exécuter directement dans le thread de la boucle libevent sous peine de geler toutes les connexions en cours ; prévoir un pool de workers ou `evthread_use_pthreads()` | R6, R7, R8, R9, R11, R13, R14 | ⬜ À faire |
| A6 | libevent / libsysrepo | Les callbacks de notification sysrepo (`sr_oper_get_items_subscribe`, `sr_event_notif_subscribe`, souscriptions) tournent sur des **threads internes à sysrepo** — prévoir un handoff thread-safe (pipe/socketpair + `event_new` sur le fd de lecture) vers la boucle libevent plutôt que de manipuler les structures nghttp2 depuis ce thread | R19, R24, R30, R32 | ⬜ À faire |
| A7 | libjwt | Ne fait pas le fetch JWKS lui-même : coder la récupération HTTP du JWKS du service d'auth, un cache indexé par `kid`, et le rafraîchissement périodique/à rotation de clé, via un client HTTP séparé | R2 | ⬜ À faire |
| A8 | libjwt | Pas de garantie de thread-safety d'un même contexte `jwt_t` réutilisé entre threads workers — créer/valider un token par requête plutôt que de partager un contexte entre threads concurrents | R2 | ⬜ À faire |
| A9 | libsysrepo (NACM) | Mapper l'identité extraite du JWT vers l'utilisateur NACM via `sr_session_set_orig_name()` (session sysrepo dédiée par requête/utilisateur), **avant** tout appel de données, sinon les règles `ietf-netconf-acm` ne s'appliquent pas au bon compte | R2, R22 | ⬜ À faire |

## Notes
- Les IDs représentent un ordre logique d'implémentation ; les colonnes "Dépendances" indiquent les points à finaliser avant de démarrer.
- R1 à R19 couvrent le cœur du protocole défini par la RFC 8040 (ressource racine, datastore, données, opérations, schéma, query params, erreurs, flux d'événements).
- R20 et R21 sont des extensions normalisées (YANG Patch, NMDA) qui s'appuient sur le cœur.
- R23 à R32 concernent les fonctionnalités avancées (call home, souscriptions, YANG-Push, binding RESTCONF des souscriptions).
