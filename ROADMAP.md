# Roadmap - Support du protocole RESTCONF (version revue et précisée)

## Objectif et statut de conformité

Cette roadmap décrit les étapes d'implémentation nécessaires pour supporter le protocole RESTCONF et ses extensions normalisées.

Elle vise une conformité fonctionnelle avec :

- le cœur du protocole RESTCONF ;
- les exigences HTTP/HTTPS et HTTP/2 sous-jacentes ;
- les mécanismes d'erreur RESTCONF ;
- NACM ;
- YANG Library ;
- NMDA ;
- YANG Patch ;
- les souscriptions YANG ;
- YANG-Push ;
- le binding RESTCONF des souscriptions ;
- RESTCONF Call Home ;
- les bonnes pratiques de sécurité associées.

Les éléments marqués `À valider` devront être confirmés par :

- une relecture normative finale ;
- des tests de conformité ;
- des tests d’interopérabilité avec des clients RESTCONF ;
- des tests de sécurité et de robustesse.

La conformité ne pourra être déclarée complète que lorsque l’ensemble des items obligatoires aura été implémenté, testé et validé.

---

## RFC couvertes

La roadmap doit se conformer aux RFC suivantes.

Les statuts utilisés sont :

- **Obligatoire** : requis pour la conformité du périmètre concerné ;
- **Conditionnel** : requis seulement si la fonctionnalité correspondante est supportée ;
- **Recommandé** : fortement conseillé pour la robustesse, la sécurité ou l’interopérabilité.

| RFC | Objet | Statut | Remarques |
|---|---|---:|---|
| [RFC 8040](https://datatracker.ietf.org/doc/html/rfc8040) | RESTCONF Protocol | Obligatoire | Cœur du protocole RESTCONF |
| [RFC 9110](https://datatracker.ietf.org/doc/html/rfc9110) | HTTP Semantics | Obligatoire | Méthodes, headers, codes, authentification HTTP, conditional requests |
| [RFC 9113](https://datatracker.ietf.org/doc/html/rfc9113) | HTTP/2 | Obligatoire | Transport HTTP/2, flux, flow control |
| [RFC 8446](https://datatracker.ietf.org/doc/html/rfc8446) | TLS 1.3 | Recommandé / Conditionnel | TLS 1.2 minimum recommandé côté proxy ; TLS 1.3 recommandé |
| [RFC 6241](https://datatracker.ietf.org/doc/html/rfc6241) | NETCONF | Obligatoire | Référence pour le registre des `error-tag` réutilisés par RESTCONF |
| [RFC 6243](https://datatracker.ietf.org/doc/html/rfc6243) | With-defaults Capability | Conditionnel | Requis si le paramètre `with-defaults` est supporté |
| [RFC 6415](https://datatracker.ietf.org/doc/html/rfc6415) | Web Host Metadata | Recommandé | Utilisé pour la découverte de la racine RESTCONF |
| [RFC 3986](https://datatracker.ietf.org/doc/html/rfc3986) | URI Generic Syntax | Obligatoire | Parsing, décodage et validation des URI RESTCONF |
| [RFC 7950](https://datatracker.ietf.org/doc/html/rfc7950) | YANG 1.1 | Obligatoire | Modèle de données YANG |
| [RFC 7951](https://datatracker.ietf.org/doc/html/rfc7951) | JSON Encoding of YANG Data | Obligatoire | Encodage JSON des données YANG |
| [RFC 7952](https://datatracker.ietf.org/doc/html/rfc7952) | JSON Metadata Annotations for YANG | Conditionnel | Utile pour NMDA `with-origin` et certaines annotations JSON |
| [RFC 8071](https://datatracker.ietf.org/doc/html/rfc8071) | NETCONF Call Home and RESTCONF Call Home | Conditionnel | Requis si RESTCONF Call Home est supporté |
| [RFC 8072](https://datatracker.ietf.org/doc/html/rfc8072) | YANG Patch Media Type | Conditionnel | Requis si YANG Patch est supporté |
| [RFC 8341](https://datatracker.ietf.org/doc/html/rfc8341) | Network Configuration Access Control Model (NACM) | Obligatoire | Contrôle d’accès transverse |
| [RFC 8342](https://datatracker.ietf.org/doc/html/rfc8342) | Network Management Datastore Architecture (NMDA) | Conditionnel | Requis si NMDA est supporté ; définit les datastores et notions d’origine |
| [RFC 8525](https://datatracker.ietf.org/doc/html/rfc8525) | YANG Library | Obligatoire | Description des modules, features, deviations, schema locations, `content-id` |
| [RFC 8527](https://datatracker.ietf.org/doc/html/rfc8527) | RESTCONF Extensions for NMDA | Conditionnel | Requis si les datastores NMDA sont exposés via RESTCONF |
| [RFC 8639](https://datatracker.ietf.org/doc/html/rfc8639) | Subscription to YANG Notifications | Conditionnel | Requis si les souscriptions YANG sont supportées |
| [RFC 8641](https://datatracker.ietf.org/doc/html/rfc8641) | Subscription to YANG Notifications for Datastore Updates (YANG-Push) | Conditionnel | Requis si YANG-Push est supporté |
| [RFC 8650](https://datatracker.ietf.org/doc/html/rfc8650) | Dynamic Subscription to YANG Events and Datastores over RESTCONF | Conditionnel | Requis si les souscriptions dynamiques RESTCONF sont supportées |
| [RFC 7519](https://datatracker.ietf.org/doc/html/rfc7519) | JSON Web Token (JWT) | Conditionnel | Requis si l’authentification par JWT est utilisée |
| [RFC 7515](https://datatracker.ietf.org/doc/html/rfc7515) | JSON Web Signature (JWS) | Conditionnel | Requis pour la validation des JWT signés |
| [RFC 7517](https://datatracker.ietf.org/doc/html/rfc7517) | JSON Web Key (JWK) | Conditionnel | Requis pour l’exploitation des JWKS |
| [RFC 8725](https://datatracker.ietf.org/doc/html/rfc8725) | JSON Web Token Best Current Practices | Conditionnel | Bonnes pratiques de sécurité JWT |
| [RFC 6750](https://datatracker.ietf.org/doc/html/rfc6750) | OAuth 2.0 Bearer Token Usage | Conditionnel | Recommandé si les JWT sont utilisés comme Bearer tokens OAuth2 |

---

## Contexte de déploiement

Le serveur RESTCONF est un backend en h2c (HTTP/2 en clair), placé derrière un reverse proxy qui gère la terminaison TLS/HTTPS.

Vis-à-vis du client final, le protocole doit rester conforme à l’exigence HTTPS de la RFC 8040, à condition que :

- le reverse proxy soit la seule voie d’accès au backend ;
- le backend h2c ne soit jamais directement exposé ;
- le proxy supporte HTTP/2 over TLS côté client ;
- HTTP/1.1 ne soit accepté que si la sémantique RESTCONF reste conforme ;
- le proxy négocie ALPN avec `h2` ;
- le proxy termine TLS avec TLS 1.2 minimum, TLS 1.3 recommandé ;
- le proxy préserve la sémantique HTTP : méthodes, headers, codes, corps, `Location`, `Accept`, `Content-Type`, `Authorization`, `ETag`, `If-Match`, `If-None-Match` ;
- les flux SSE et souscriptions longues ne soient pas cassés par des timeouts proxy inadaptés ;
- le proxy ne bufferise pas excessivement les flux SSE ;
- les fermetures de flux HTTP/2 (`RST_STREAM`) soient correctement propagées.

L’émission du JWT est déléguée à un service tiers en amont, mais c’est le backend RESTCONF qui valide lui-même l’authenticité du token.

Le backend doit :

- vérifier la signature du JWT conformément à RFC 7515 ;
- récupérer les clés via JWKS conformément à RFC 7517 ;
- valider les claims standards : `exp`, `iss`, `aud`, `nbf` si présent ;
- valider le `kid` et gérer la rotation de clés ;
- refuser les algorithmes non sûrs, notamment `none` ou les algorithmes ambigus ;
- extraire l’identité et les rôles/groupes pour l’autorisation NACM ;
- mapper explicitement l’identité JWT vers un utilisateur/groupe NACM ;
- prévoir une protection contre le rejeu si nécessaire, par exemple via `jti` ou une fenêtre de validité courte.

---

## Remarques de conformité importantes

Les points suivants ont été ajoutés ou précisés par rapport à la version précédente.

### 1. TLS et reverse proxy

La conformité RESTCONF côté client dépend fortement du reverse proxy.

Le proxy doit être considéré comme un composant normatif du déploiement. Il doit :

- terminer TLS ;
- supporter ALPN `h2` ;
- ne jamais exposer le backend h2c ;
- préserver les flux SSE ;
- préserver les headers d’authentification et de négociation de contenu ;
- appliquer des timeouts compatibles avec les souscriptions longues.

### 2. JWT Bearer n’est pas un mécanisme d’authentification défini par RFC 8040

L’utilisation de JWT Bearer est acceptable comme mécanisme d’authentification HTTP, mais elle ne fait pas partie des mécanismes définis par RFC 8040.

Elle doit donc être traitée comme une extension d’implémentation et être conforme aux RFC JWT/JWS/JWK applicables.

### 3. Capacités RESTCONF

La roadmap doit explicitement prévoir l’exposition des capacités RESTCONF dans :

```text
{+restconf}/restconf-state/capabilities
```

Les capacités peuvent inclure, selon les fonctionnalités supportées :

- `with-defaults` ;
- `depth` ;
- `fields` ;
- `filter` ;
- YANG Patch ;
- NMDA ;
- souscriptions ;
- YANG-Push.

### 4. NMDA nécessite RFC 8342

RFC 8527 étend RESTCONF pour NMDA, mais les concepts de datastores et d’origine proviennent de RFC 8342.

La roadmap doit donc référencer RFC 8342 lorsque NMDA est supporté.

Le comportement de `{+restconf}/data` doit être explicitement documenté et validé lorsque les datastores NMDA sont aussi exposés via `{+restconf}/ds/<datastore>`.

### 5. Table d’erreurs RESTCONF

La table `error-tag` → code HTTP doit être centralisée et validée contre RFC 8040.

Certains codes HTTP peuvent dépendre du contexte :

- `401` pour authentification absente ou invalide, avec `WWW-Authenticate` ;
- `403` pour authentification valide mais accès refusé ;
- `404` pour ressource inexistante ;
- `405` pour méthode non autorisée sur la ressource, avec `Allow` ;
- `406` pour `Accept` non supporté ;
- `412` pour échec de précondition HTTP ;
- `415` pour `Content-Type` non supporté ;
- `500` pour erreur interne ou opérationnelle.

### 6. Souscriptions configurées

RFC 8639 ne couvre pas uniquement les souscriptions dynamiques créées par RPC.

Si une conformité complète à RFC 8639 est visée, il faut aussi prévoir :

- les souscriptions configurées ;
- leur persistance ;
- leur état après redémarrage ;
- les notifications de cycle de vie associées.

Si les souscriptions configurées ne sont pas supportées, cela doit être explicitement documenté.

### 7. YANG Library et changements à chaud

Si les modules YANG peuvent être installés ou retirés à chaud, le serveur doit :

- mettre à jour la YANG Library ;
- mettre à jour `content-id` ;
- émettre la notification de changement de YANG Library si applicable ;
- garantir la cohérence entre les modules chargés et les `location` exposées.

### 8. SSE, HTTP/2 et flow control

Les flux SSE sur HTTP/2 doivent respecter le flow control HTTP/2.

Chaque DATA frame doit respecter la fenêtre de crédit du stream. En cas de blocage, le serveur doit pouvoir reprendre l’envoi après `WINDOW_UPDATE`.

Les timeouts proxy doivent être configurés pour ne pas interrompre les flux SSE légitimes.

---

## Roadmap d’implémentation

| ID | RFC + § | Description | Dépendances | Statut | Avancement |
|---|---|---|---|---:|---|
| R1 | [RFC 8040 §2](https://datatracker.ietf.org/doc/html/rfc8040#section-2), [RFC 9110](https://datatracker.ietf.org/doc/html/rfc9110), [RFC 9113](https://datatracker.ietf.org/doc/html/rfc9113), [RFC 8446](https://datatracker.ietf.org/doc/html/rfc8446) - Transport et TLS | Backend en h2c derrière un reverse proxy qui termine TLS. Le proxy doit supporter HTTP/2 over TLS avec ALPN `h2`, TLS 1.2 minimum, TLS 1.3 recommandé. Il doit parler h2c en prior knowledge vers le backend. Le backend ne doit jamais être directement accessible. Le proxy doit préserver la sémantique HTTP, les headers sensibles, les flux SSE et les timeouts longs. | — | Obligatoire | 🟡 En cours — squelette h2c (libevent+nghttp2) fonctionnel côté backend (`h2c_server.c`) ; volet TLS/reverse proxy à documenter/valider |
| R2 | [RFC 7519](https://datatracker.ietf.org/doc/html/rfc7519), [RFC 7515](https://datatracker.ietf.org/doc/html/rfc7515), [RFC 7517](https://datatracker.ietf.org/doc/html/rfc7517), [RFC 8725](https://datatracker.ietf.org/doc/html/rfc8725), [RFC 6750](https://datatracker.ietf.org/doc/html/rfc6750) - Identification du client | Le backend valide le JWT reçu dans `Authorization: Bearer`. Validation de la signature JWS, récupération des clés via JWKS, gestion du `kid`, validation de `exp`, `iss`, `aud`, `nbf`, rejet des algorithmes non sûrs. Extraction de l’identité et des groupes pour NACM. Si les tokens sont des Bearer tokens OAuth2, appliquer RFC 6750. | R1 | Conditionnel si JWT | ⬜ À valider |
| R3 | [RFC 8040 §3.1](https://datatracker.ietf.org/doc/html/rfc8040#section-3.1), [RFC 6415](https://datatracker.ietf.org/doc/html/rfc6415) - Root Resource Discovery | Découverte de la racine `{+restconf}` via `/.well-known/host-meta`. Endpoint non authentifié. La réponse doit contenir un lien avec la relation `restconf` pointant vers la racine RESTCONF. Support optionnel mais recommandé de `/.well-known/host-meta.json`. | R1 | Recommandé | ✅ Terminé — implémenté dans `well-known.c` (XRD + JSON, négociation `Accept`, GET/HEAD/OPTIONS) ; tests de conformité restants |
| R4 | [RFC 8040 §3.3](https://datatracker.ietf.org/doc/html/rfc8040#section-3.3) - API Resource | Exposer la ressource racine RESTCONF avec les sous-ressources `data` et `operations`. Les ressources de monitoring, notamment `restconf-state`, `capabilities` et `streams`, doivent être exposées selon les fonctionnalités supportées. Valider les URI exactes conformément à RFC 8040 et aux modules YANG de monitoring. | R3 | Obligatoire | ⬜ À valider |
| R5 | [RFC 8040 §3.4](https://datatracker.ietf.org/doc/html/rfc8040#section-3.4), [RFC 8342](https://datatracker.ietf.org/doc/html/rfc8342), [RFC 8527](https://datatracker.ietf.org/doc/html/rfc8527) - Datastore Resource | Exposer `{+restconf}/data` comme vue du datastore conceptuel. Si NMDA est supporté, documenter et valider le comportement de `/data` par rapport aux datastores NMDA exposés via `/ds/<datastore>`. Préciser le comportement en lecture, écriture, config, non-config et operational. | R4 | Obligatoire | ⬜ À valider |
| R6 | [RFC 8040 §4.3](https://datatracker.ietf.org/doc/html/rfc8040#section-4.3) - GET : lecture simple | Lecture d’une ressource de données simple (leaf/container). Support de HEAD, mêmes règles sans corps. Respect de la négociation de contenu et des media types RESTCONF. | R5, R41 | Obligatoire | ⬜ À valider |
| R7 | [RFC 8040 §4.3](https://datatracker.ietf.org/doc/html/rfc8040#section-4.3), [RFC 7951](https://datatracker.ietf.org/doc/html/rfc7951) - GET : listes/leaf-lists | Lecture de listes et leaf-lists. En XML, la réponse doit contenir un seul élément racine. En JSON, encodage conforme à RFC 7951. | R6 | Obligatoire | ⬜ À valider |
| R8 | [RFC 8040 §4.3](https://datatracker.ietf.org/doc/html/rfc8040#section-4.3), [RFC 8341](https://datatracker.ietf.org/doc/html/rfc8341) - GET : masquage NACM | Lecture partielle autorisée : omettre le contenu non lisible plutôt que renvoyer une erreur globale, sauf si la ressource cible entière est inaccessible. Valider précisément le comportement pour les listes, clés, feuilles `config false` et sous-arbres partiellement visibles. | R6, R29 | Obligatoire | ⬜ À valider |
| R9 | [RFC 8040 §3.4.1, §3.5.1-2](https://datatracker.ietf.org/doc/html/rfc8040#section-3.5.1), [RFC 9110](https://datatracker.ietf.org/doc/html/rfc9110) - Conditional Requests | Générer `ETag` et `Last-Modified` sur les réponses GET/HEAD. Les validateurs doivent être déterministes et cohérents par ressource/datastore. Supporter `If-Match`, `If-None-Match`, `If-Modified-Since`, `If-Unmodified-Since` lorsque pertinent. Retourner `304 Not Modified` ou `412 Precondition Failed` selon le cas. Voir A11. | R6, R42 | Obligatoire | ⬜ À valider |
| R10 | [RFC 8040 §4.4.1](https://datatracker.ietf.org/doc/html/rfc8040#section-4.4.1) - POST : Create Resource Mode | Création d’une ressource enfant via POST sur le parent. Retourner `201 Created` avec header `Location`. Si la ressource existe déjà, retourner `409 Conflict` avec `error-tag=data-exists`. | R5, R41 | Obligatoire | ⬜ À valider |
| R11 | [RFC 8040 §4.4.2](https://datatracker.ietf.org/doc/html/rfc8040#section-4.4.2) - POST : Invoke Operation Mode (action) | Invocation d’une action YANG définie sur un data resource via POST. Retourner `200 OK` avec corps si sortie, sinon `204 No Content`. Appliquer NACM `exec`. | R5, R41, R29 | Obligatoire | ⬜ À valider |
| R12 | [RFC 8040 §4.5](https://datatracker.ietf.org/doc/html/rfc8040#section-4.5) - Data Resource : PUT | Création ou remplacement complet d’une ressource de données. Respect des préconditions HTTP. Retourner `201 Created` si création, `204 No Content` si remplacement. | R5, R41, R42 | Obligatoire | ⬜ À valider |
| R13 | [RFC 8040 §4.7](https://datatracker.ietf.org/doc/html/rfc8040#section-4.7) - Data Resource : DELETE | Suppression d’une ressource existante. Retourner `204 No Content`. Gérer accès, ressource absente, conflits, verrous et préconditions. | R5, R42 | Obligatoire | ⬜ À valider |
| R14 | [RFC 8040 §4.6](https://datatracker.ietf.org/doc/html/rfc8040#section-4.6) - PATCH (plain patch) | Support du plain patch RESTCONF. Le plain patch peut fusionner et/ou créer des sous-ressources. Ne pas retourner systématiquement `404` si la cible n’existe pas. Retourner `200 OK` ou `204 No Content` selon le cas. | R6, R12, R41 | Obligatoire | ⬜ À valider |
| R15 | [RFC 8040 §3.6](https://datatracker.ietf.org/doc/html/rfc8040#section-3.6) - Operations Resource | Exposer la ressource `operations`. Supporter `GET {+restconf}/operations` pour découvrir les RPC disponibles, `OPTIONS` pour les méthodes autorisées, et `POST {+restconf}/operations/<rpc>` pour invoquer un RPC. Retourner `200 OK` avec corps si sortie, sinon `204 No Content`. Appliquer NACM `exec`. | R4, R40, R29 | Obligatoire | ⬜ À valider |
| R16 | [RFC 8525](https://datatracker.ietf.org/doc/html/rfc8525) - YANG Library | Exposer la YANG Library décrivant modules, sous-modules, features, deviations, `content-id` et URLs de schéma. La YANG Library doit être cohérente avec l’état réel du serveur et exposée sur le datastore opérationnel. Si les modules changent à chaud, mettre à jour `content-id` et émettre la notification de changement si applicable. Voir R48. | R5 | Obligatoire | ⬜ À valider |
| R17 | [RFC 8040 §3.7](https://datatracker.ietf.org/doc/html/rfc8040#section-3.7) - Schema Resource (GET) | Endpoint GET servant le code source d’un module YANG avec le media type `application/yang`. Valider la forme exacte de l’URI, par exemple `{+restconf}/yang/{module}@{revision}.yang`. Les URLs annoncées dans la YANG Library doivent être réellement accessibles. | R4, R16 | Conditionnel / Recommandé | ⬜ À valider |
| R18 | [RFC 8040 §3.7](https://datatracker.ietf.org/doc/html/rfc8040#section-3.7) - `get-schema` (optionnel) | Alternative via l’opération RPC `get-schema` du module `ietf-netconf-monitoring`, exposée sur `/operations`, pour les modules sans URL de `location` statique. | R15 | Optionnel | ⬜ À valider |
| R19 | [RFC 8040 §4.8.1-4.8.3, §4.8.9](https://datatracker.ietf.org/doc/html/rfc8040#section-4.8.1), [RFC 6243](https://datatracker.ietf.org/doc/html/rfc6243) - Query Params : mise en forme du GET | Paramètres `content` (`config`, `nonconfig`, `all`), `depth` (entier ou `unbounded`), `fields`, `with-defaults` (`report-all`, `trim`, `explicit`, et `report-all-tagged` si applicable). Les paramètres de requête inconnus doivent être ignorés. Valider l’applicabilité de `report-all-tagged` en JSON. | R6, R7, R43 | Obligatoire | ⬜ À valider |
| R20 | [RFC 8040 §4.8.5, §4.8.6](https://datatracker.ietf.org/doc/html/rfc8040#section-4.8.5) - Query Params : ordonnancement | Paramètres `insert` (`first`, `last`, `before`, `after`) et `point` pour les listes/leaf-lists `ordered-by user`. `point` est requis pour `before`/`after`. | R10, R12 | Obligatoire | ⬜ À valider |
| R21 | [RFC 8040 §4.8.4, §4.8.7, §4.8.8](https://datatracker.ietf.org/doc/html/rfc8040#section-4.8.4) - Query Params : flux d’événements | Paramètres `filter`, `start-time`, `stop-time` pour les ressources de type stream. `start-time`/`stop-time` ne sont utilisables que si le replay est supporté par le stream. | R24 | Conditionnel si streams | ⬜ À valider |
| R22 | [RFC 8040 §7](https://datatracker.ietf.org/doc/html/rfc8040#section-7), [RFC 9110](https://datatracker.ietf.org/doc/html/rfc9110) - Error Reporting | Modèle d’erreurs `ietf-restconf:errors`. Centraliser le mapping `error-tag` → code HTTP → enveloppe JSON/XML. Inclure les headers requis : `WWW-Authenticate` pour `401`, `Allow` pour `405`. Gérer `406`, `415`, `412`. Les erreurs après établissement d’un flux SSE ne peuvent plus être renvoyées via une enveloppe RESTCONF classique. | Plusieurs | Obligatoire | ⬜ À valider |
| R23 | [RFC 8040 §3.8, §9.2](https://datatracker.ietf.org/doc/html/rfc8040#section-3.8) - Event Stream : découverte des flux | Exposer la liste des flux disponibles via le module de monitoring RESTCONF, typiquement `ietf-restconf-monitoring:restconf-state/streams/stream`. Inclure nom, description, support du replay, URL d’accès. Appliquer NACM en lecture. Valider l’URI exacte. | R6, R16, R29, R45 | Conditionnel si notifications | ⬜ À valider |
| R24 | [RFC 8040 §6.2-6.3](https://datatracker.ietf.org/doc/html/rfc8040#section-6.2), [RFC 9113](https://datatracker.ietf.org/doc/html/rfc9113) - Event Stream : établissement du flux SSE | GET sur une ressource stream avec `Accept: text/event-stream`. Réponse `200 OK` + `Content-Type: text/event-stream`. Maintenir la connexion HTTP/2 ouverte. Respecter le flow control HTTP/2. Prévoir heartbeats/commentaires SSE. Configurer les timeouts proxy. | R23, R41 | Conditionnel si notifications | ⬜ À valider |
| R25 | [RFC 8040 §6.4](https://datatracker.ietf.org/doc/html/rfc8040#section-6.4) - Event Stream : relais des notifications | Abonnement sysrepo aux notifications YANG, formatage et relais vers le flux SSE. Données encodées selon le media type négocié. Inclure `eventTime`. Nettoyage des souscriptions lors de la fermeture du flux. | R24, R29 | Conditionnel si notifications | ⬜ À valider |
| R26 | [RFC 8072](https://datatracker.ietf.org/doc/html/rfc8072) - YANG Patch Media Type | Édition de plusieurs sous-ressources en une seule requête PATCH avec `application/yang-patch+json` ou `application/yang-patch+xml`. Supporter `create`, `delete`, `insert`, `merge`, `move`, `remove`, `replace`. Répondre avec `ietf-yang-patch:yang-patch-status`, incluant `global-errors` ou `edit-status`. Ne pas réutiliser brutalement `ietf-restconf:errors`. | R14, R41 | Conditionnel si YANG Patch | ⬜ À valider |
| R27 | [RFC 8527 §3.1](https://datatracker.ietf.org/doc/html/rfc8527#section-3.1), [RFC 8342](https://datatracker.ietf.org/doc/html/rfc8342) - NMDA : datastores additionnels | Support des ressources `{+restconf}/ds/<datastore>` selon les datastores réellement supportés : `operational`, `candidate`, `startup`, `intended`, etc. Documenter les datastores supportés et leurs permissions. | R5, R6, R29 | Conditionnel si NMDA | ⬜ À valider |
| R28 | [RFC 8527 §3.2, §4](https://datatracker.ietf.org/doc/html/rfc8527#section-3.2), [RFC 7952](https://datatracker.ietf.org/doc/html/rfc7952), [RFC 8342](https://datatracker.ietf.org/doc/html/rfc8342) - NMDA : paramètre `with-origin` | Indique l’origine de chaque valeur dans les réponses GET lorsque pertinent. L’origine doit être matérialisée selon le format d’annotation prévu, notamment annotations JSON si JSON. | R27, R41 | Conditionnel si NMDA | ⬜ À valider |
| R29 | [RFC 8341](https://datatracker.ietf.org/doc/html/rfc8341) - NACM | Contrôle d’accès NACM appliqué à toutes les opérations RESTCONF : GET, HEAD, POST, PUT, PATCH, DELETE, RPC, actions, notifications, streams et souscriptions. Mapper l’identité JWT vers un utilisateur/groupe NACM. Positionner l’identité de session sysrepo avant tout accès aux données. Appliquer les règles `read`, `write`, `exec`. Prévoir des règles par défaut sûres. | R2 | Obligatoire | ⬜ À valider |
| R30 | [RFC 8071](https://datatracker.ietf.org/doc/html/rfc8071), [RFC 8446](https://datatracker.ietf.org/doc/html/rfc8446) - RESTCONF Call Home | Connexion sortante initiée par le serveur vers le client de gestion. TLS côté client sortant, authentification mutuelle si requis, validation stricte des certificats, trust anchors, retries, backoff, timeouts. Après établissement, appliquer NACM à la session. | R1 | Conditionnel si Call Home | ⬜ À valider |
| R31 | [RFC 8639 §2.4.1](https://datatracker.ietf.org/doc/html/rfc8639#section-2.4.1) - `establish-subscription` | RPC de création d’une souscription dynamique. Modèle d’état `/subscriptions`. Gestion des filtres, encodage, transport. Appliquer NACM. | R15, R29 | Conditionnel si souscriptions | ⬜ À valider |
| R32 | [RFC 8639 §2.4.2-2.4.4](https://datatracker.ietf.org/doc/html/rfc8639#section-2.4.2) - `modify` / `delete` / `kill-subscription` | RPC de gestion du cycle de vie d’une souscription existante. | R31 | Conditionnel si souscriptions | ⬜ À valider |
| R33 | [RFC 8639 §2.4.5-2.4.6](https://datatracker.ietf.org/doc/html/rfc8639#section-2.4.5) - Notifications de cycle de vie | Notifications de cycle de vie : `subscription-started` si applicable, `subscription-terminated`, `subscription-modified`, `subscription-resumed`, `subscription-suspended`, `replay-completed` si pertinent. | R31 | Conditionnel si souscriptions | ⬜ À valider |
| R34 | [RFC 8641 §2.1](https://datatracker.ietf.org/doc/html/rfc8641#section-2.1) - YANG-Push : mode on-change | Souscription déclenchée par changement dans le datastore. Support de `dampening-period`, filtres de sélection, notifications `push-change-update`. Documenter les datastores et nœuds réellement supportés. | R31, R27 | Conditionnel si YANG-Push | ⬜ À valider |
| R35 | [RFC 8641 §2.2](https://datatracker.ietf.org/doc/html/rfc8641#section-2.2) - YANG-Push : mode périodique | Souscription à échantillonnage périodique avec `period`. Notifications `push-update`. | R31, R27 | Conditionnel si YANG-Push | ⬜ À valider |
| R36 | [RFC 8641 §3.6](https://datatracker.ietf.org/doc/html/rfc8641#section-3.6) - `resync-subscription` | RPC permettant de forcer un renvoi complet de l’état courant. Valider si la resynchronisation s’applique uniquement aux souscriptions on-change ou aussi à d’autres modes. | R34, R35 | Conditionnel si YANG-Push | ⬜ À valider |
| R37 | [RFC 8650 §2.1-2.2](https://datatracker.ietf.org/doc/html/rfc8650#section-2.1) - Binding RESTCONF : établissement | `establish-subscription` invoqué via POST sur `/operations`. Valider le code de succès attendu selon RFC 8650 : `200 OK` avec sortie, ou `201 Created` + `Location` si applicable. Retourner l’identifiant de souscription et/ou l’URI SSE dédiée. | R31, R15 | Conditionnel si souscriptions RESTCONF | ⬜ À valider |
| R38 | [RFC 8650 §2.3](https://datatracker.ietf.org/doc/html/rfc8650#section-2.3) - Binding RESTCONF : gestion | `modify-subscription`, `delete-subscription`, `kill-subscription` via POST sur `/operations`, cohérent avec R32. | R32, R37 | Conditionnel si souscriptions RESTCONF | ⬜ À valider |
| R39 | [RFC 8650 §3](https://datatracker.ietf.org/doc/html/rfc8650#section-3) - Binding RESTCONF : réception | GET SSE sur l’URI de souscription pour recevoir les notifications poussées. L’URI doit être protégée, associée à l’identité authentifiée et à la souscription. Nettoyage lors de la fermeture du flux. | R37, R24 | Conditionnel si souscriptions RESTCONF | ⬜ À valider |
| R40 | [RFC 8040](https://datatracker.ietf.org/doc/html/rfc8040), [RFC 9110](https://datatracker.ietf.org/doc/html/rfc9110) - OPTIONS et Allow | Implémenter OPTIONS sur les ressources RESTCONF et retourner un header `Allow` correct. Toute réponse `405 Method Not Allowed` doit également inclure `Allow`. Centraliser les méthodes autorisées par type de ressource. | R4, R5 | Obligatoire | ⬜ À valider |
| R41 | [RFC 8040](https://datatracker.ietf.org/doc/html/rfc8040), [RFC 7951](https://datatracker.ietf.org/doc/html/rfc7951) - Media types et négociation de contenu | Supporter les media types RESTCONF `application/yang-data+json` et `application/yang-data+xml` pour une conformité stricte. Gérer `Accept`, `Content-Type`, `406 Not Acceptable`, `415 Unsupported Media Type`. Renvoyer les erreurs dans le même media type que la requête lorsque possible. | R1 | Obligatoire | ⬜ À valider |
| R42 | [RFC 9110](https://datatracker.ietf.org/doc/html/rfc9110) - Sémantique HTTP transverse | Prise en compte transverse de la sémantique HTTP : méthodes, headers conditionnels, validateurs, `304`, `412`, `405`, `401` + `WWW-Authenticate`, `406`, `415`, `Location`, `Allow`. | R1 | Obligatoire | ⬜ À valider |
| R43 | [RFC 8040 §3.5.3](https://datatracker.ietf.org/doc/html/rfc8040#section-3.5.3), [RFC 3986](https://datatracker.ietf.org/doc/html/rfc3986) - Parsing URI / api-path | Décoder correctement le chemin RESTCONF avant toute transformation XPath : percent-encoding, clés de listes, virgules échappées, caractères spéciaux. Rejeter les chemins invalides. Ignorer les query parameters inconnus. | R5 | Obligatoire | ⬜ À valider |
| R44 | [RFC 8040 §2](https://datatracker.ietf.org/doc/html/rfc8040#section-2), [RFC 8725](https://datatracker.ietf.org/doc/html/rfc8725), [RFC 9113](https://datatracker.ietf.org/doc/html/rfc9113), [RFC 8446](https://datatracker.ietf.org/doc/html/rfc8446) - Durcissement sécurité | Limiter `SETTINGS_MAX_CONCURRENT_STREAMS`, taille des headers, taille des corps, nombre de connexions/streams par client, fréquence de validation JWT, cache JWKS, protection contre rejeu, timeouts adaptés aux flux longs. Ajouter rate-limiting des RPC coûteux et des souscriptions. | R1, R2 | Obligatoire | 🟡 Amorcé — `SETTINGS_MAX_CONCURRENT_STREAMS`/fenêtres HTTP/2 configurés (`Config.in`) ; `H2C_MAX_REQUEST_BODY_SIZE` défini mais pas encore appliqué ; JWT/rate-limiting restent à faire |
| R45 | [RFC 8040 §3.8](https://datatracker.ietf.org/doc/html/rfc8040#section-3.8) - RESTCONF capabilities | Exposer les capacités RESTCONF dans `restconf-state/capabilities`. Annoncer les capacités supportées : `with-defaults`, `depth`, `fields`, `filter`, YANG Patch, NMDA, souscriptions, YANG-Push, etc. | R4, R16 | Obligatoire | ⬜ À faire |
| R46 | [RFC 8342](https://datatracker.ietf.org/doc/html/rfc8342), [RFC 8527](https://datatracker.ietf.org/doc/html/rfc8527) - Mapping NMDA de `/data` | Documenter et tester le comportement de `{+restconf}/data` lorsque NMDA est supporté. Préciser le datastore cible pour les écritures, le datastore lu pour les GET, et le traitement des données `config false`, `operational`, `intended`, `candidate`, `startup`. | R5, R27 | Conditionnel si NMDA | ⬜ À valider |
| R47 | [RFC 8639](https://datatracker.ietf.org/doc/html/rfc8639) - Souscriptions configurées | Si une conformité complète RFC 8639 est visée, supporter les souscriptions configurées : configuration persistante, état, reprise au démarrage, notifications associées. Si non supporté, documenter explicitement que seules les souscriptions dynamiques sont disponibles. | R31 | Conditionnel | ⬜ À valider |
| R48 | [RFC 8525](https://datatracker.ietf.org/doc/html/rfc8525) - Notification de changement YANG Library | Si les modules peuvent changer à chaud, mettre à jour `content-id`, rafraîchir la YANG Library et émettre la notification de changement de YANG Library si applicable. | R16 | Conditionnel si modules dynamiques | ⬜ À faire |
| R49 | [RFC 8650](https://datatracker.ietf.org/doc/html/rfc8650), [RFC 8341](https://datatracker.ietf.org/doc/html/rfc8341) - Sécurité des URI de souscription | Les URI SSE de souscription doivent être protégées, associées à l’identité authentifiée, liées à l’identifiant de souscription, nettoyées à expiration/suppression/fermeture, et soumises à NACM. | R37, R39, R29 | Conditionnel si souscriptions | ⬜ À faire |
| R50 | [RFC 9110](https://datatracker.ietf.org/doc/html/rfc9110) - Cache HTTP | Définir une politique de cache sûre pour les réponses RESTCONF. Pour les données sensibles ou volatiles, prévoir par exemple `Cache-Control: no-store` ou équivalent. | R42 | Recommandé | ⬜ À faire |
| R51 | Toutes RFC - Tests de conformité | Ajouter et exécuter une suite de tests couvrant : discovery, media types, erreurs, NACM, conditional requests, SSE, souscriptions, YANG-Push, NMDA, YANG Library, OPTIONS, sécurité TLS/proxy. | Tous | Obligatoire | ⬜ À faire |

---

## Légende avancement

- ⬜ À faire
- 🟡 En cours
- ✅ Terminé
- ⛔ Bloqué

---

## Points d’attention techniques (stack libevent + libnghttp2 + libsysrepo + libjwt)

| ID | Composant | Point d’attention | R lié | Avancement |
|---|---|---|---|---|
| A1 | libnghttp2 | Lib bas niveau : câbler `send_callback` / `recv_callback` sur des `bufferevent` libevent, parser les pseudo-headers (`:method`, `:path`, `:authority`, `:scheme`), assembler HEADERS+DATA en requête exploitable. | R1 | ✅ Terminé — `send_cb`, `on_header`, `on_data_chunk_recv`, `on_frame_recv` implémentés dans `h2c_server.c` (parsing `:method`/`:path` via libcurl) |
| A2 | libnghttp2 / reverse proxy | Le proxy doit parler h2c en prior knowledge vers le backend, sans Upgrade HTTP/1.1. | R1 | 🟡 En cours — côté backend `nghttp2_session_server_new2` prêt à recevoir du h2c prior-knowledge ; configuration/validation côté reverse proxy à faire |
| A3 | libnghttp2 | Flow control HTTP/2 sur les flux SSE : chaque DATA frame doit respecter la fenêtre de crédit du stream, sinon blocage silencieux ; relancer l’envoi via `nghttp2_session_resume_data()` sur WINDOW_UPDATE. | R24, R25, R39 | ⬜ À faire |
| A4 | libnghttp2 | Pas de support SSE natif : encoder soi-même les lignes `data: ...\n\n` dans les DATA frames et positionner `Content-Type: text/event-stream`. | R24, R25, R39 | ⬜ À faire |
| A5 | libevent / libsysrepo | Les appels `sr_get_data`, `sr_edit_batch`, `sr_apply_changes`, `sr_rpc_send` sont synchrones/bloquants — à ne pas exécuter directement dans le thread de la boucle libevent ; prévoir un pool de workers ou `evthread_use_pthreads()`. | R6, R7, R8, R9, R10, R11, R12, R13, R15, R17, R18 | ⬜ À faire |
| A6 | libevent / libsysrepo | Les callbacks de notification sysrepo tournent sur des threads internes — prévoir un handoff thread-safe vers la boucle libevent plutôt que de manipuler directement les structures nghttp2 depuis ces threads. | R25, R31, R37, R39 | ⬜ À faire |
| A7 | libjwt | Ne fait pas le fetch JWKS lui-même : coder la récupération HTTP du JWKS, un cache indexé par `kid`, et le rafraîchissement périodique/à rotation de clé. | R2 | ⬜ À faire |
| A8 | libjwt | Pas de garantie de thread-safety d’un même contexte `jwt_t` réutilisé entre threads workers — créer/valider un token par requête plutôt que partager un contexte entre threads concurrents. | R2 | ⬜ À faire |
| A9 | libsysrepo (NACM) | Mapper l’identité extraite du JWT vers l’utilisateur NACM via `sr_session_set_orig_name()` avant tout appel de données, sinon les règles NACM ne s’appliquent pas au bon compte. | R2, R29 | ⬜ À faire |
| A10 | Gestion d’erreurs (R22) | Centraliser le mapping `error-tag` → code HTTP → enveloppe JSON/XML `ietf-restconf:errors` dans une fonction/table unique en C, appelée par tous les handlers. | R22 | 🟡 Amorcé — `h2c_send_error()` centralise l'envoi des codes HTTP nus (avec `WWW-Authenticate` sur 401/403) ; l'enveloppe JSON/XML `ietf-restconf:errors` reste à implémenter |
| A11 | libsysrepo | sysrepo ne fournit pas nativement d’`ETag` / `Last-Modified` par ressource — les maintenir soi-même : compteur de révision, hash de sous-arbre, ou mécanisme équivalent. Les validateurs doivent être cohérents par ressource/datastore. | R5, R9 | ⬜ À faire |
| A12 | Parsing URI | Le décodage du `api-path` doit être fait avant toute construction de XPath libyang — ne jamais concaténer directement le path brut dans un `lyd_new_path` / `lys_find_path`. | R6, R7, R8, R9, R10, R11, R12, R13, R14, R43 | ⬜ À faire |
| A13 | libsysrepo | Le coût d’un `sr_session_start()` par requête peut devenir non négligeable — évaluer un pool de sessions sysrepo indexées par identité JWT. | R2, R29 | ⬜ À faire |
| A14 | libnghttp2 / libjwt | La validation JWT RS256/ES256 est coûteuse ; combinée au multiplexing HTTP/2, c’est un vecteur de DoS — limiter explicitement les streams concurrents et les connexions par IP. | R1, R2, R44 | ⬜ À faire |
| A15 | RFC 8072 (YANG Patch) | Le format d’erreur de YANG Patch (`ietf-yang-patch:yang-patch-status`) est différent de l’enveloppe générique `ietf-restconf:errors` — prévoir un formatteur dédié. | R26 | ⬜ À faire |
| A16 | libevent / libnghttp2 (SSE) | Détecter la fermeture anticipée d’un flux SSE côté client pour désabonner proprement la souscription sysrepo correspondante. Configurer le timeout de lecture du reverse proxy pour ne pas interrompre les connexions SSE légitimes. | R24, R25, R31, R37, R39 | ⬜ À faire |
| A17 | libsysrepo / YANG Library | Le leaf `content-id` de la YANG Library doit changer à chaque install/remove de module à chaud — s’abonner aux notifications de changement de contexte sysrepo ou invalider un cache local. | R16, R48 | ⬜ À faire |
| A18 | reverse proxy | Le proxy doit supporter HTTP/2 over TLS avec ALPN, terminer le TLS, ne jamais exposer le backend h2c directement, préserver les headers HTTP et préserver les flux SSE longs. | R1, R24, R39 | ⬜ À faire |
| A19 | media types | Centraliser la négociation de contenu et la sélection du media type de réponse, y compris pour les erreurs. | R41, R22 | ⬜ À faire |
| A20 | OPTIONS / Allow | Centraliser la liste des méthodes autorisées par type de ressource pour répondre correctement à OPTIONS et aux erreurs `405`. | R40, R22 | 🟡 En cours — primitive générique `h2c_send_options()` disponible et déjà utilisée par `well-known.c` ; à généraliser aux futures ressources de données |
| A21 | souscriptions | Maintenir un état propre des souscriptions : identifiants, URI SSE, encodage, filtres, nettoyage des souscriptions orphelines. | R31, R37, R39 | ⬜ À faire |
| A22 | RESTCONF capabilities | Maintenir la liste des capacités RESTCONF exposées dans `restconf-state/capabilities` en cohérence avec les fonctionnalités réellement activées. | R45 | ⬜ À faire |
| A23 | NMDA | Documenter et tester le mapping de `/data` et `/ds/<datastore>` ; vérifier le comportement avec `operational`, `running`, `candidate`, `startup`, `intended`. | R5, R27, R46 | ⬜ À faire |
| A24 | souscriptions configurées | Si supportées, persister les souscriptions configurées et restaurer leur état au démarrage. | R47 | ⬜ À faire |
| A25 | sécurité JWT | Whitelister les algorithmes JWT, refuser `none`, valider `aud`, `iss`, `exp`, `nbf`, `kid`, et prévoir une politique de cache JWKS sécurisée. | R2, R44 | ⬜ À faire |
| A26 | sécurité souscriptions | Lier les URI SSE aux identités authentifiées et aux identifiants de souscription ; empêcher l’accès à une souscription par un autre utilisateur. | R39, R49 | ⬜ À faire |

---

## Référence : codes d’erreur RESTCONF (détail de R22)

### Cas particulier des Event Streams (SSE)

Une fois la réponse HTTP envoyée et le flux `text/event-stream` établi, le serveur ne peut plus modifier le code de statut HTTP.

Les erreurs détectées pendant la durée de vie du flux doivent donc être traitées par :

- la fermeture de la connexion ;
- ou les mécanismes prévus par les RFC de souscription (RFC 8639 / RFC 8650) ;
- et non par l’enveloppe `ietf-restconf:errors`.

### Format de l’enveloppe d’erreur

Toute erreur est renvoyée dans le corps de la réponse avec le même media type que la requête (`application/yang-data+json` ou `application/yang-data+xml`), sous la racine `ietf-restconf:errors` :

```json
{
  "ietf-restconf:errors": {
    "error": [
      {
        "error-type": "protocol",
        "error-tag": "invalid-value",
        "error-path": "/example-ops:input/delay",
        "error-message": "Invalid input parameter"
      }
    ]
  }
}
```

Champs :

- `error-type` : `transport` | `rpc` | `protocol` | `application` ;
- `error-tag` : voir table ci-dessous ;
- `error-app-tag`, `error-path`, `error-message`, `error-info` : optionnels, à remplir si disponibles.

Pour YANG Patch, utiliser le format spécifique `ietf-yang-patch:yang-patch-status` et non l’enveloppe générique ci-dessus.

### Table de correspondance error-tag → code HTTP

Cette table doit être validée contre RFC 8040. Les codes ci-dessous sont une proposition opérationnelle.

| error-tag | Code(s) HTTP | Contexte typique |
|---|---:|---|
| `in-use` | 409 | Ressource verrouillée ou opération conflictuelle en cours |
| `invalid-value` | 400, parfois 404 ou 406 selon contexte | Valeur invalide, ressource inexistante, représentation invalide |
| `too-big` | 413 | Message de requête ou réponse trop volumineux |
| `missing-attribute` | 400 | Attribut requis absent |
| `bad-attribute` | 400 | Attribut invalide |
| `unknown-attribute` | 400 | Attribut inconnu |
| `missing-element` | 400 | Élément requis absent |
| `bad-element` | 400 | Élément invalide |
| `unknown-element` | 400 | Élément inconnu |
| `unknown-namespace` | 400 | Namespace incorrect |
| `access-denied` | 403, ou 401 si authentification absente/invalide | Accès refusé ; `401` doit inclure `WWW-Authenticate` |
| `lock-denied` | 409 | Ressource verrouillée |
| `resource-denied` | 403, à valider selon contexte | Opération refusée pour raison de politique ou ressource non disponible ; si erreur interne de capacité, `500` peut être plus approprié |
| `rollback-failed` | 500 | Échec du rollback après une erreur d’édition |
| `data-exists` | 409 | Ressource déjà existante, notamment POST en mode création |
| `data-missing` | 409 | Ressource attendue absente dans un contexte de conflit |
| `operation-not-supported` | 405, parfois 501 selon contexte | Méthode ou opération non supportée ; `405` doit inclure `Allow` |
| `operation-failed` | 500, parfois 412 selon contexte | Échec logique de l’opération, validation métier, exécution RPC, échec de précondition |
| `malformed-message` | 400 | Requête syntaxiquement invalide, JSON/XML mal formé |

Notes HTTP importantes :

- `401` doit s’accompagner du header `WWW-Authenticate` ;
- `405` doit s’accompagner du header `Allow` ;
- `406` est utilisé si le media type demandé via `Accept` n’est pas supporté ;
- `415` est utilisé si le `Content-Type` de la requête n’est pas supporté ;
- `412` est utilisé en cas d’échec de précondition HTTP (`If-Match`, etc.) ;
- les erreurs RESTCONF doivent être encodées dans le media type de la requête lorsque possible.

---

## Codes attendus par méthode (résumé opérationnel)

| Méthode | Succès | Erreurs principales |
|---|---|---|
| OPTIONS (R40) | `204 No Content` ou `200 OK` + header `Allow` | `405` si OPTIONS non géré localement, avec `Allow` |
| GET / HEAD (R6-R9) | `200 OK` + corps, HEAD sans corps | `400`, `401`, `403`, `404`, `405`, `406`, `412` |
| POST création (R10) | `201 Created` + header `Location`, sans corps | `400`, `401`, `403`, `404`, `409` (`data-exists`), `415` |
| POST invocation RPC / action (R11, R15) | `200 OK` + corps si sortie, sinon `204 No Content` | `400`, `401`, `403`, `404`, `412`, `415` |
| PUT (R12) | `201 Created` si création, `204 No Content` si remplacement | `400`, `401`, `403`, `404`, `409`, `412`, `415` |
| PATCH plain patch (R14) | `200 OK` ou `204 No Content` | `400`, `401`, `403`, `404`, `409`, `412`, `415` |
| DELETE (R13) | `204 No Content` | `401`, `403`, `404`, `409`, `412` |
| PATCH YANG Patch (R26) | `200 OK` + `yang-patch-status` | Erreurs dans `yang-patch-status`, ou `400`, `401`, `403`, `404`, `409`, `412`, `415` |

---

## Tests de conformité par composant

Cette section décrit les tests de conformité à prévoir pour valider chaque composant de l’implémentation RESTCONF.

Les tests doivent être :

- automatisés autant que possible ;
- exécutés dans un environnement représentatif ;
- rejouables après chaque évolution du serveur, du proxy ou des modules YANG ;
- complétés par des tests manuels pour les cas limites TLS/HTTP/2/SSE.

Chaque test doit vérifier :

- le code HTTP attendu ;
- les headers HTTP requis ;
- le media type de réponse ;
- le corps JSON/XML conforme ;
- l’absence de fuite de données soumises à NACM ;
- la cohérence des erreurs RESTCONF.

Toutes les lignes sont initialisées avec l’avancement `⬜ À faire`.

---

### 1. Transport, TLS, HTTP/2 et reverse proxy

**Items liés :** R1, R44, A1, A2, A3, A16, A18
**RFC liées :** RFC 8040 §2, RFC 9110, RFC 9113

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-TRANS-01 | Accès à la racine RESTCONF en HTTPS avec ALPN `h2` | La connexion HTTP/2 over TLS est établie, le serveur répond correctement | ✅ Terminé |
| T-TRANS-02 | Accès direct au backend h2c depuis l’extérieur | L’accès direct doit être impossible : filtrage réseau, proxy seul point d’entrée | ✅ Terminé |
| T-TRANS-03 | Requête HTTP/2 avec pseudo-headers `:method`, `:path`, `:authority`, `:scheme` | Le backend reconstruit correctement la requête RESTCONF | ✅ Terminé |
| T-TRANS-04 | Requête HTTP/1.1 si supportée par le proxy | La sémantique RESTCONF reste conforme ; sinon le proxy doit refuser proprement | ✅ Terminé |
| T-TRANS-05 | Préservation du header `Authorization: Bearer` par le proxy | Le backend reçoit le JWT intact et peut le valider | ✅ Terminé |
| T-TRANS-06 | Préservation des headers `Accept`, `Content-Type`, `If-Match`, `If-None-Match`, `Location` | Les headers ne sont ni supprimés ni altérés | 🟡 En cours — `If-Match` non couvert explicitement, seul `If-None-Match` est testé |
| T-TRANS-07 | Ouverture d’un flux SSE long | Le proxy ne coupe pas le flux prématurément si des heartbeats sont émis | 🟡 En cours — test écrit mais `skip` (endpoint SSE non implémenté) |
| T-TRANS-08 | Bufferisation proxy sur SSE | Les événements SSE sont transmis sans délai anormal | 🟡 En cours — test écrit mais `skip` (endpoint SSE non implémenté) |
| T-TRANS-09 | Fermeture du flux HTTP/2 par le client (`RST_STREAM`) | Le serveur détecte la fermeture et nettoie les ressources associées | ✅ Terminé |
| T-TRANS-10 | Dépassement de `SETTINGS_MAX_CONCURRENT_STREAMS` | Le serveur refuse ou limite les streams supplémentaires conformément à HTTP/2 | ✅ Terminé |
| T-TRANS-11 | Headers HTTP trop volumineux | Le serveur rejette la requête avec une erreur HTTP appropriée | ✅ Terminé |
| T-TRANS-12 | Corps de requête trop volumineux | Le serveur rejette avec `413 Payload Too Large` ou erreur équivalente | ✅ Terminé |
| T-TRANS-13 | Timeout proxy inférieur à la durée d’un flux SSE | Le test doit démontrer que le heartbeat ou la configuration proxy évite la coupure | 🟡 En cours — test écrit mais `skip` (endpoint SSE non implémenté) |
| T-TRANS-14 | TLS 1.0/1.1 si interdits | La connexion est refusée | ✅ Terminé |
| T-TRANS-15 | ALPN sans `h2` si HTTP/2 est requis | La connexion est refusée ou dégradée uniquement si explicitement supporté | ✅ Terminé |

---

### 2. Découverte de la racine RESTCONF

**Items liés :** R3
**RFC liées :** RFC 8040 §3.1, RFC 6415

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-DISC-01 | GET non authentifié sur `/.well-known/host-meta` | Réponse valide contenant un lien de relation `restconf` | ✅ Terminé |
| T-DISC-02 | Vérification du lien `restconf` | Le lien pointe vers la racine RESTCONF correcte | ✅ Terminé |
| T-DISC-03 | GET sur `/.well-known/host-meta.json` si supporté | Réponse JSON valide avec le lien `restconf` | ✅ Terminé |
| T-DISC-04 | Accès à `/.well-known/host-meta` avec un token invalide | L’endpoint reste accessible ou retourne une erreur cohérente, sans exposer de données sensibles | ✅ Terminé |
| T-DISC-05 | Absence de données sensibles dans host-meta | La réponse ne contient aucune information de configuration ou d’état | ✅ Terminé |

---

### 3. Ressource racine API et capacités

**Items liés :** R4, R15, R40
**RFC liées :** RFC 8040 §3.3, §3.6, §3.8

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-API-01 | GET sur `{+restconf}` | La ressource racine est retournée dans le media type négocié | ✅ Terminé |
| T-API-02 | Vérification des sous-ressources annoncées | `data` et `operations` sont présentes ; `streams` ou `restconf-state` selon l’implémentation | ✅ Terminé |
| T-API-03 | OPTIONS sur la racine | Réponse avec header `Allow` contenant au minimum `GET`, `OPTIONS` | ✅ Terminé |
| T-API-04 | GET sur `{+restconf}/restconf-state/capabilities` si supporté | Les capacités RESTCONF sont listées et cohérentes avec les fonctionnalités activées | ✅ Terminé |
| T-API-05 | Cohérence des capacités annoncées | Si YANG Patch, NMDA, with-defaults, depth, fields ou subscriptions sont supportés, ils sont annoncés | ✅ Terminé |
| T-API-06 | Accès non autorisé à la racine si authentification requise | `401 Unauthorized` avec `WWW-Authenticate` ou `403 Forbidden` selon le cas | ✅ Terminé |

---

### 4. Datastore et lecture GET simple

**Items liés :** R5, R6, R7, R41, R43
**RFC liées :** RFC 8040 §3.4, §4.3, RFC 7951

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-GET-01 | GET sur `{+restconf}/data` | Retourne la vue du datastore conceptuel conformément à la politique NACM | ⬜ À faire |
| T-GET-02 | GET sur un container | Réponse `200 OK` avec encodage JSON ou XML correct | ⬜ À faire |
| T-GET-03 | GET sur une leaf | Réponse `200 OK` avec la valeur encodée correctement | ⬜ À faire |
| T-GET-04 | HEAD sur une ressource | Mêmes headers que GET, sans corps | ⬜ À faire |
| T-GET-05 | GET sur une liste | En JSON, tableau conforme RFC 7951 ; en XML, élément racine unique | ⬜ À faire |
| T-GET-06 | GET sur une leaf-list | Encodage correct selon JSON/XML | ⬜ À faire |
| T-GET-07 | GET avec `Accept: application/yang-data+json` | Réponse en JSON YANG | ⬜ À faire |
| T-GET-08 | GET avec `Accept: application/yang-data+xml` | Réponse en XML YANG | ⬜ À faire |
| T-GET-09 | GET sur ressource inexistante | `404 Not Found` avec erreur RESTCONF pertinente | ⬜ À faire |
| T-GET-10 | GET avec chemin percent-encodé | Le chemin est correctement décodé avant traitement | ⬜ À faire |
| T-GET-11 | GET avec clés de liste contenant virgules ou caractères spéciaux | Le parsing est correct et sécurisé | ⬜ À faire |
| T-GET-12 | GET avec chemin invalide | `400 Bad Request` ou `404 Not Found` selon le cas, avec enveloppe d’erreur | ⬜ À faire |

---

### 5. Paramètres de requête GET

**Items liés :** R19, R20, R21, R43
**RFC liées :** RFC 8040 §4.8, RFC 6243

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-QUERY-01 | GET avec `content=config` | Seules les données de configuration sont retournées | ⬜ À faire |
| T-QUERY-02 | GET avec `content=nonconfig` | Seules les données d’état sont retournées | ⬜ À faire |
| T-QUERY-03 | GET avec `content=all` | Données de configuration et d’état retournées | ⬜ À faire |
| T-QUERY-04 | GET avec `depth=1` | Seuls les enfants directs sont inclus | ⬜ À faire |
| T-QUERY-05 | GET avec `depth=unbounded` | L’arbre complet est retourné | ⬜ À faire |
| T-QUERY-06 | GET avec `fields` valide | Seuls les champs demandés sont retournés | ⬜ À faire |
| T-QUERY-07 | GET avec `fields` invalide | Erreur `400 Bad Request` avec `error-tag` pertinent | ⬜ À faire |
| T-QUERY-08 | GET avec `with-defaults=report-all` | Valeurs par défaut incluses selon RFC 6243 | ⬜ À faire |
| T-QUERY-09 | GET avec `with-defaults=trim` | Valeurs égales au default retirées si applicable | ⬜ À faire |
| T-QUERY-10 | GET avec `with-defaults=explicit` | Seules les valeurs explicitement positionnées sont retournées | ⬜ À faire |
| T-QUERY-11 | GET avec `with-defaults=report-all-tagged` si supporté | Annotations ou marquage conformes au mode supporté | ⬜ À faire |
| T-QUERY-12 | GET avec paramètre de requête inconnu | Le paramètre inconnu est ignoré | ⬜ À faire |
| T-QUERY-13 | GET avec combinaison `content`, `depth`, `fields` | La réponse respecte simultanément les trois paramètres | ⬜ À faire |
| T-QUERY-14 | GET sur stream avec `start-time`/`stop-time` | Accepté uniquement si le replay est supporté | ⬜ À faire |
| T-QUERY-15 | GET sur stream sans replay mais avec `start-time` | Erreur RESTCONF pertinente | ⬜ À faire |

---

### 6. Requêtes conditionnelles

**Items liés :** R9, R42, A11
**RFC liées :** RFC 8040 §3.4.1, §3.5.1-2, RFC 9110

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-COND-01 | GET sur une ressource | Présence de `ETag` et/ou `Last-Modified` si supporté | ⬜ À faire |
| T-COND-02 | GET avec `If-None-Match` égal à l’ETag courant | `304 Not Modified` sans corps | ⬜ À faire |
| T-COND-03 | GET avec `If-None-Match` différent | `200 OK` avec corps | ⬜ À faire |
| T-COND-04 | PUT avec `If-Match` valide | Modification acceptée | ⬜ À faire |
| T-COND-05 | PUT avec `If-Match` invalide | `412 Precondition Failed` | ⬜ À faire |
| T-COND-06 | DELETE avec `If-Match` valide | Suppression acceptée | ⬜ À faire |
| T-COND-07 | DELETE avec `If-Match` invalide | `412 Precondition Failed` | ⬜ À faire |
| T-COND-08 | GET avec `If-Modified-Since` antérieur à la modification | `200 OK` | ⬜ À faire |
| T-COND-09 | GET avec `If-Modified-Since` postérieur à la modification | `304 Not Modified` si applicable | ⬜ À faire |
| T-COND-10 | Modification de la ressource puis relecture | L’ETag ou le `Last-Modified` change | ⬜ À faire |
| T-COND-11 | Validateurs sur ressources NMDA | Les validateurs sont cohérents par datastore si pertinent | ⬜ À faire |

---

### 7. Écritures : POST, PUT, DELETE, plain PATCH

**Items liés :** R10, R11, R12, R13, R14, R20, R41, R42
**RFC liées :** RFC 8040 §4.4, §4.5, §4.6, §4.7

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-WRITE-01 | POST création d’une ressource enfant | `201 Created` avec header `Location` | ⬜ À faire |
| T-WRITE-02 | POST création d’une ressource déjà existante | `409 Conflict` avec `error-tag=data-exists` | ⬜ À faire |
| T-WRITE-03 | POST avec corps invalide | `400 Bad Request` avec erreur RESTCONF | ⬜ À faire |
| T-WRITE-04 | POST avec `Content-Type` non supporté | `415 Unsupported Media Type` | ⬜ À faire |
| T-WRITE-05 | PUT création d’une ressource | `201 Created` | ⬜ À faire |
| T-WRITE-06 | PUT remplacement d’une ressource existante | `204 No Content` | ⬜ À faire |
| T-WRITE-07 | PUT avec précondition `If-Match` invalide | `412 Precondition Failed` | ⬜ À faire |
| T-WRITE-08 | DELETE d’une ressource existante | `204 No Content` | ⬜ À faire |
| T-WRITE-09 | DELETE d’une ressource inexistante | `404 Not Found` | ⬜ À faire |
| T-WRITE-10 | DELETE avec précondition invalide | `412 Precondition Failed` | ⬜ À faire |
| T-WRITE-11 | Plain PATCH sur ressource existante | Fusion réussie, `200 OK` ou `204 No Content` | ⬜ À faire |
| T-WRITE-12 | Plain PATCH créant des sous-ressources | Création/fusion réussie sans `404` systématique | ⬜ À faire |
| T-WRITE-13 | Plain PATCH avec corps invalide | `400 Bad Request` | ⬜ À faire |
| T-WRITE-14 | POST invocation d’une action YANG | `200 OK` avec sortie ou `204 No Content` sans sortie | ⬜ À faire |
| T-WRITE-15 | POST action avec input invalide | `400 Bad Request` | ⬜ À faire |
| T-WRITE-16 | Création dans liste `ordered-by user` avec `insert`/`point` | L’ordre demandé est respecté | ⬜ À faire |
| T-WRITE-17 | `insert=before` sans `point` | Erreur `400 Bad Request` | ⬜ À faire |
| T-WRITE-18 | Écriture sur ressource non autorisée | `403 Forbidden` ou `401 Unauthorized` selon authentification | ⬜ À faire |

---

### 8. Operations, RPC et actions

**Items liés :** R11, R15, R40
**RFC liées :** RFC 8040 §3.6, §4.4.2

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-OPS-01 | GET sur `{+restconf}/operations` | Liste des RPC disponibles, filtrée selon NACM | ⬜ À faire |
| T-OPS-02 | OPTIONS sur `{+restconf}/operations` | Header `Allow` correct | ⬜ À faire |
| T-OPS-03 | POST sur un RPC avec input valide | `200 OK` avec output ou `204 No Content` | ⬜ À faire |
| T-OPS-04 | POST sur un RPC sans output | `204 No Content` | ⬜ À faire |
| T-OPS-05 | POST sur un RPC avec input invalide | `400 Bad Request` | ⬜ À faire |
| T-OPS-06 | POST sur un RPC inconnu | `404 Not Found` ou erreur RESTCONF pertinente | ⬜ À faire |
| T-OPS-07 | POST sur un RPC non autorisé | `403 Forbidden` | ⬜ À faire |
| T-OPS-08 | POST sur une action liée à un data resource | Exécution correcte si la ressource parent existe | ⬜ À faire |
| T-OPS-09 | POST action sur ressource parent inexistante | `404 Not Found` | ⬜ À faire |
| T-OPS-10 | RPC avec output partiellement masqué par NACM | Comportement validé : omission ou erreur selon la règle applicable | ⬜ À faire |

---

### 9. Media types et négociation de contenu

**Items liés :** R41, R22, A19
**RFC liées :** RFC 8040, RFC 7951

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-MEDIA-01 | Requête avec `Accept: application/yang-data+json` | Réponse JSON YANG | ⬜ À faire |
| T-MEDIA-02 | Requête avec `Accept: application/yang-data+xml` | Réponse XML YANG | ⬜ À faire |
| T-MEDIA-03 | Requête avec `Accept` non supporté | `406 Not Acceptable` | ⬜ À faire |
| T-MEDIA-04 | Requête avec `Content-Type` non supporté | `415 Unsupported Media Type` | ⬜ À faire |
| T-MEDIA-05 | Erreur sur requête JSON | Erreur renvoyée en `application/yang-data+json` si possible | ⬜ À faire |
| T-MEDIA-06 | Erreur sur requête XML | Erreur renvoyée en `application/yang-data+xml` si possible | ⬜ À faire |
| T-MEDIA-07 | Corps JSON mal formé | `400 Bad Request` avec `malformed-message` ou équivalent | ⬜ À faire |
| T-MEDIA-08 | Corps XML mal formé | `400 Bad Request` avec `malformed-message` ou équivalent | ⬜ À faire |
| T-MEDIA-09 | Réponse SSE | `Content-Type: text/event-stream` | ⬜ À faire |
| T-MEDIA-10 | YANG Patch | `Content-Type` `application/yang-patch+json` ou `application/yang-patch+xml` accepté | ⬜ À faire |

---

### 10. Gestion des erreurs RESTCONF

**Items liés :** R22, A10
**RFC liées :** RFC 8040 §7, RFC 9110

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-ERR-01 | Erreur de protocole | Enveloppe `ietf-restconf:errors` présente | ⬜ À faire |
| T-ERR-02 | Vérification de `error-type` | Valeur cohérente : `transport`, `rpc`, `protocol`, `application` | ⬜ À faire |
| T-ERR-03 | Vérification de `error-tag` | Tag conforme à la table de correspondance | ⬜ À faire |
| T-ERR-04 | Erreur avec chemin fautif | `error-path` renseigné si pertinent | ⬜ À faire |
| T-ERR-05 | Erreur avec message lisible | `error-message` présent si activé | ⬜ À faire |
| T-ERR-06 | `401 Unauthorized` | Header `WWW-Authenticate` présent | ⬜ À faire |
| T-ERR-07 | `405 Method Not Allowed` | Header `Allow` présent | ⬜ À faire |
| T-ERR-08 | `406 Not Acceptable` | Media type demandé non supporté | ⬜ À faire |
| T-ERR-09 | `415 Unsupported Media Type` | `Content-Type` non supporté | ⬜ À faire |
| T-ERR-10 | `412 Precondition Failed` | Échec de précondition HTTP | ⬜ À faire |
| T-ERR-11 | `409 Conflict` avec `data-exists` | POST création sur ressource existante | ⬜ À faire |
| T-ERR-12 | `500 Internal Server Error` avec `operation-failed` | Erreur interne ou opérationnelle | ⬜ À faire |
| T-ERR-13 | Erreur après établissement SSE | Pas d’enveloppe RESTCONF classique ; fermeture ou mécanisme de souscription | ⬜ À faire |
| T-ERR-14 | Erreur YANG Patch | Utilisation de `yang-patch-status`, pas de l’enveloppe générique seule | ⬜ À faire |

---

### 11. NACM

**Items liés :** R8, R29, A9, A13
**RFC liées :** RFC 8341

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-NACM-01 | Requête sans token si authentification requise | `401 Unauthorized` avec `WWW-Authenticate` | ⬜ À faire |
| T-NACM-02 | Requête avec token valide mais utilisateur non autorisé | `403 Forbidden` | ⬜ À faire |
| T-NACM-03 | GET sur ressource partiellement lisible | Les nœuds non autorisés sont omis | ⬜ À faire |
| T-NACM-04 | GET sur ressource cible entièrement interdite | `403 Forbidden` | ⬜ À faire |
| T-NACM-05 | GET sur liste avec entrées partiellement autorisées | Seules les entrées autorisées sont retournées | ⬜ À faire |
| T-NACM-06 | GET sur feuille `config false` interdite | La feuille est omise ou erreur selon règle validée | ⬜ À faire |
| T-NACM-07 | POST création interdite | `403 Forbidden` | ⬜ À faire |
| T-NACM-08 | PUT/PATCH/DELETE interdits | `403 Forbidden` | ⬜ À faire |
| T-NACM-09 | RPC interdit | `403 Forbidden` | ⬜ À faire |
| T-NACM-10 | Action interdite | `403 Forbidden` | ⬜ À faire |
| T-NACM-11 | Découverte de stream interdite | Stream non visible ou accès refusé | ⬜ À faire |
| T-NACM-12 | Établissement de souscription interdit | Erreur d’autorisation | ⬜ À faire |
| T-NACM-13 | Mapping JWT vers groupe NACM | Les règles NACM appliquées correspondent au groupe extrait du token | ⬜ À faire |
| T-NACM-14 | Session sysrepo avec identité correcte | `sr_session_set_orig_name()` positionné avant accès données | ⬜ À faire |
| T-NACM-15 | Règles par défaut sûres | En l’absence de règle explicite, le comportement est sécurisé | ⬜ À faire |

---

### 12. YANG Library et schema

**Items liés :** R16, R17, R18, A17
**RFC liées :** RFC 8525, RFC 8040 §3.7

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-YLIB-01 | GET sur la YANG Library | Modules, révisions, features, deviations et `content-id` sont exposés | ⬜ À faire |
| T-YLIB-02 | Exposition sur datastore opérationnel | La YANG Library est lue depuis l’opérationnel | ⬜ À faire |
| T-YLIB-03 | Cohérence avec modules chargés | Les modules exposés correspondent à l’état réel sysrepo/libyang | ⬜ À faire |
| T-YLIB-04 | URLs `location` accessibles | Les URLs de schéma retournées sont réellement accessibles | ⬜ À faire |
| T-YLIB-05 | GET sur un module YANG | Réponse `200 OK` avec `Content-Type: application/yang` | ⬜ À faire |
| T-YLIB-06 | GET sur module inconnu | `404 Not Found` | ⬜ À faire |
| T-YLIB-07 | GET sur module avec révision | La bonne révision est servie | ⬜ À faire |
| T-YLIB-08 | Installation/retrait de module à chaud | `content-id` change | ⬜ À faire |
| T-YLIB-09 | Notification de changement YANG Library si supportée | Notification émise lors d’un changement de module set | ⬜ À faire |
| T-YLIB-10 | RPC `get-schema` si supporté | Retourne le schéma demandé | ⬜ À faire |
| T-YLIB-11 | NACM sur YANG Library | Accès filtré si des règles s’appliquent | ⬜ À faire |

---

### 13. NMDA

**Items liés :** R5, R27, R28
**RFC liées :** RFC 8527, RFC 7952

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-NMDA-01 | GET sur `{+restconf}/ds/operational` | Retourne les données opérationnelles si supporté | ⬜ À faire |
| T-NMDA-02 | GET sur `{+restconf}/ds/candidate` | Retourne les données candidate si supporté | ⬜ À faire |
| T-NMDA-03 | GET sur `{+restconf}/ds/startup` | Retourne les données startup si supporté | ⬜ À faire |
| T-NMDA-04 | GET sur `{+restconf}/ds/intended` | Retourne les données intended si supporté | ⬜ À faire |
| T-NMDA-05 | GET sur datastore non supporté | Erreur RESTCONF pertinente | ⬜ À faire |
| T-NMDA-06 | Comparaison `/data` et `/ds/<datastore>` | Le comportement de `/data` est conforme à la documentation et à RFC 8527 | ⬜ À faire |
| T-NMDA-07 | Écriture via `/ds/<datastore>` si supporté | Le datastore cible est correctement modifié | ⬜ À faire |
| T-NMDA-08 | GET avec `with-origin` | Annotations d’origine présentes si supporté | ⬜ À faire |
| T-NMDA-09 | `with-origin` en JSON | Annotations JSON conformes RFC 7952 | ⬜ À faire |
| T-NMDA-10 | NACM sur datastores | Les règles NACM s’appliquent aussi via `/ds/<datastore>` | ⬜ À faire |
| T-NMDA-11 | Conditional requests sur datastores | ETag/Last-Modified cohérents par datastore si implémenté | ⬜ À faire |

---

### 14. YANG Patch

**Items liés :** R26, A15
**RFC liées :** RFC 8072

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-YPATCH-01 | PATCH avec `application/yang-patch+json` | Requête acceptée | ⬜ À faire |
| T-YPATCH-02 | PATCH avec `application/yang-patch+xml` | Requête acceptée | ⬜ À faire |
| T-YPATCH-03 | Opération `create` | Ressource créée | ⬜ À faire |
| T-YPATCH-04 | Opération `delete` | Ressource supprimée | ⬜ À faire |
| T-YPATCH-05 | Opération `merge` | Fusion réussie | ⬜ À faire |
| T-YPATCH-06 | Opération `replace` | Remplacement réussi | ⬜ À faire |
| T-YPATCH-07 | Opération `remove` | Suppression si présente, sans erreur si absente selon sémantique | ⬜ À faire |
| T-YPATCH-08 | Opération `insert`/`move` | Ordre respecté pour nœuds `ordered-by user` | ⬜ À faire |
| T-YPATCH-09 | Plusieurs sous-opérations dans un même PATCH | Réponse `yang-patch-status` cohérente | ⬜ À faire |
| T-YPATCH-10 | Erreur sur une sous-opération | `edit-status` contient l’erreur par opération | ⬜ À faire |
| T-YPATCH-11 | Erreur globale de requête | `global-errors` renseigné | ⬜ À faire |
| T-YPATCH-12 | YANG Patch non autorisé | Erreur NACM | ⬜ À faire |
| T-YPATCH-13 | YANG Patch avec media type incorrect | `415 Unsupported Media Type` | ⬜ À faire |

---

### 15. Event streams et SSE

**Items liés :** R23, R24, R25, R21, A3, A4, A6, A16
**RFC liées :** RFC 8040 §3.8, §6.2-6.4

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-SSE-01 | GET sur la liste des streams | Liste des flux disponibles | ⬜ À faire |
| T-SSE-02 | Vérification des métadonnées de stream | Nom, description, replay-support, URL d’accès présents | ⬜ À faire |
| T-SSE-03 | GET sur un stream avec `Accept: text/event-stream` | `200 OK` et `Content-Type: text/event-stream` | ⬜ À faire |
| T-SSE-04 | Réception d’une notification YANG | Événement SSE formaté correctement | ⬜ À faire |
| T-SSE-05 | Vérification de `eventTime` | Horodatage présent et cohérent | ⬜ À faire |
| T-SSE-06 | Encodage JSON/XML des notifications | Conforme au media type négocié | ⬜ À faire |
| T-SSE-07 | Heartbeat SSE | Commentaire ou événement maintenant la connexion active | ⬜ À faire |
| T-SSE-08 | Stream avec replay supporté | `start-time` accepté | ⬜ À faire |
| T-SSE-09 | Stream sans replay mais avec `start-time` | Erreur RESTCONF | ⬜ À faire |
| T-SSE-10 | `stop-time` atteint | Fin de replay ou fermeture conforme | ⬜ À faire |
| T-SSE-11 | Notification `replay-completed` si applicable | Émise à la fin du replay | ⬜ À faire |
| T-SSE-12 | Fermeture du flux par le client | La souscription sysrepo est nettoyée | ⬜ À faire |
| T-SSE-13 | Flux SSE sous HTTP/2 avec flow control | Les DATA frames respectent la fenêtre de crédit | ⬜ À faire |
| T-SSE-14 | WINDOW_UPDATE après blocage | L’envoi reprend via `nghttp2_session_resume_data()` | ⬜ À faire |
| T-SSE-15 | Stream interdit par NACM | Accès refusé ou stream non visible | ⬜ À faire |

---

### 16. Souscriptions dynamiques

**Items liés :** R31, R32, R33, R37, R38, R39
**RFC liées :** RFC 8639, RFC 8650

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-SUB-01 | POST `establish-subscription` avec paramètres valides | Souscription créée, identifiant retourné | ⬜ À faire |
| T-SUB-02 | Vérification du code de succès | `200 OK` ou `201 Created` + `Location` selon RFC 8650 et implémentation validée | ⬜ À faire |
| T-SUB-03 | `establish-subscription` avec filtre invalide | Erreur RESTCONF | ⬜ À faire |
| T-SUB-04 | `establish-subscription` avec encodage non supporté | Erreur RESTCONF | ⬜ À faire |
| T-SUB-05 | `modify-subscription` valide | Souscription modifiée | ⬜ À faire |
| T-SUB-06 | `delete-subscription` valide | Souscription supprimée | ⬜ À faire |
| T-SUB-07 | `kill-subscription` valide | Souscription terminée | ⬜ À faire |
| T-SUB-08 | Notification `subscription-started` si applicable | Émise conformément à RFC 8639 | ⬜ À faire |
| T-SUB-09 | Notification `subscription-terminated` | Émise à la terminaison | ⬜ À faire |
| T-SUB-10 | Notification `subscription-suspended` | Émise si suspension | ⬜ À faire |
| T-SUB-11 | Notification `subscription-resumed` | Émise si reprise | ⬜ À faire |
| T-SUB-12 | État visible dans `/subscriptions` | Souscriptions dynamiques visibles si exposées | ⬜ À faire |
| T-SUB-13 | GET sur l’URI SSE de souscription | Notifications reçues | ⬜ À faire |
| T-SUB-14 | Accès à l’URI SSE par un autre utilisateur | Refusé | ⬜ À faire |
| T-SUB-15 | Souscription interdite par NACM | Erreur d’autorisation | ⬜ À faire |
| T-SUB-16 | Fermeture SSE côté client | Nettoyage de la souscription | ⬜ À faire |
| T-SUB-17 | Souscriptions orphelines | Aucune souscription ne reste active après fermeture anormale | ⬜ À faire |
| T-SUB-18 | Souscriptions configurées si supportées | Persistance et restauration au démarrage | ⬜ À faire |

---

### 17. YANG-Push

**Items liés :** R34, R35, R36
**RFC liées :** RFC 8641

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-PUSH-01 | Souscription périodique | Notifications `push-update` reçues à la période demandée | ⬜ À faire |
| T-PUSH-02 | Souscription on-change | Notifications `push-change-update` reçues lors des modifications | ⬜ À faire |
| T-PUSH-03 | `dampening-period` | Les changements rapprochés sont agrégés selon la période | ⬜ À faire |
| T-PUSH-04 | Filtre de sélection | Seules les données filtrées sont poussées | ⬜ À faire |
| T-PUSH-05 | Filtre invalide | Erreur RESTCONF | ⬜ À faire |
| T-PUSH-06 | `resync-subscription` | Renvoi complet de l’état courant si supporté | ⬜ À faire |
| T-PUSH-07 | Datastore non supporté | Erreur pertinente | ⬜ À faire |
| T-PUSH-08 | Nœud non supporté en on-change | Erreur ou exclusion documentée | ⬜ À faire |
| T-PUSH-09 | Modifications groupées | Notifications cohérentes avec la capacité du serveur | ⬜ À faire |
| T-PUSH-10 | NACM sur YANG-Push | Les données non autorisées ne sont pas poussées | ⬜ À faire |
| T-PUSH-11 | Souscription sur datastore opérationnel | Données opérationnelles correctes | ⬜ À faire |
| T-PUSH-12 | Arrêt de la souscription | Plus aucune notification envoyée | ⬜ À faire |

---

### 18. RESTCONF Call Home

**Items liés :** R30
**RFC liées :** RFC 8071

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-CH-01 | Le serveur initie une connexion sortante | Connexion TLS établie vers le client de gestion | ⬜ À faire |
| T-CH-02 | Validation du certificat du client de gestion | Échec si certificat invalide ou non fiable | ⬜ À faire |
| T-CH-03 | Authentification mutuelle si requise | La session est établie uniquement si les deux côtés sont authentifiés | ⬜ À faire |
| T-CH-04 | Trust anchors configurés | Seules les autorités approuvées sont acceptées | ⬜ À faire |
| T-CH-05 | Échec de connexion | Retries avec backoff et journalisation | ⬜ À faire |
| T-CH-06 | Timeouts | La connexion sortante ne bloque pas indéfiniment | ⬜ À faire |
| T-CH-07 | Session Call Home établie | Les opérations RESTCONF sont applicables sur la session | ⬜ À faire |
| T-CH-08 | NACM après établissement | Les opérations distantes sont autorisées selon NACM | ⬜ À faire |
| T-CH-09 | Fermeture par le client de gestion | Nettoyage de la session côté serveur | ⬜ À faire |

---

### 19. Authentification JWT et JWKS

**Items liés :** R2, R29, A7, A8, A14
**RFC liées :** RFC 7519, RFC 8725

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-JWT-01 | Token valide | Accès autorisé | ⬜ À faire |
| T-JWT-02 | Token expiré | `401 Unauthorized` | ⬜ À faire |
| T-JWT-03 | Signature invalide | `401 Unauthorized` | ⬜ À faire |
| T-JWT-04 | Claim `iss` incorrect | `401 Unauthorized` | ⬜ À faire |
| T-JWT-05 | Claim `aud` incorrect | `401 Unauthorized` | ⬜ À faire |
| T-JWT-06 | Claim `nbf` futur | `401 Unauthorized` | ⬜ À faire |
| T-JWT-07 | `kid` inconnu | Rejet ou rafraîchissement JWKS puis nouvelle validation | ⬜ À faire |
| T-JWT-08 | Rotation de clé JWKS | Le serveur récupère la nouvelle clé et valide les nouveaux tokens | ⬜ À faire |
| T-JWT-09 | Algorithme `none` | Rejet systématique | ⬜ À faire |
| T-JWT-10 | Algorithme non autorisé | Rejet | ⬜ À faire |
| T-JWT-11 | Confusion HMAC/RSA | Rejet | ⬜ À faire |
| T-JWT-12 | Cache JWKS | Les clés sont mises en cache de manière sécurisée | ⬜ À faire |
| T-JWT-13 | Rejeu de token si protection activée | Token déjà utilisé rejeté | ⬜ À faire |
| T-JWT-14 | Groupes JWT vers NACM | Le mapping est correct | ⬜ À faire |
| T-JWT-15 | Token sans groupe suffisant | Accès refusé via NACM | ⬜ À faire |
| T-JWT-16 | Multiplication de requêtes avec tokens invalides | Limitation appliquée | ⬜ À faire |

---

### 20. Sécurité, robustesse et HTTP/2

**Items liés :** R44, A3, A14, A16
**RFC liées :** RFC 9110, RFC 9113, RFC 8725

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-SEC-01 | Nombre maximal de streams concurrents | Limite respectée | ⬜ À faire |
| T-SEC-02 | Nombre maximal de connexions par client | Limite respectée | ⬜ À faire |
| T-SEC-03 | Headers HTTP surdimensionnés | Rejet | ⬜ À faire |
| T-SEC-04 | Corps HTTP surdimensionné | Rejet | ⬜ À faire |
| T-SEC-05 | Requêtes JWT coûteuses en rafale | Rate-limiting ou protection DoS | ⬜ À faire |
| T-SEC-06 | Client lent maintenant un flux ouvert | Timeout ou limitation | ⬜ À faire |
| T-SEC-07 | Flux SSE nombreux par utilisateur | Limite appliquée | ⬜ À faire |
| T-SEC-08 | RPC coûteux appelés en boucle | Limitation ou rejet contrôlé | ⬜ À faire |
| T-SEC-09 | Filtres `fields` ou XPath complexes | Protection contre consommation excessive | ⬜ À faire |
| T-SEC-10 | Fermeture brutale TCP | Nettoyage des sessions et souscriptions | ⬜ À faire |
| T-SEC-11 | RST_STREAM HTTP/2 | Nettoyage des ressources | ⬜ À faire |
| T-SEC-12 | WINDOW_UPDATE malveillant ou bloquant | Flow control respecté, pas de blocage indéfini | ⬜ À faire |
| T-SEC-13 | Logs de sécurité | Échecs d’authentification et autorisation journalisés | ⬜ À faire |
| T-SEC-14 | Absence de fuite dans les erreurs | Les messages d’erreur n’exposent pas d’informations sensibles | ⬜ À faire |

---

### 21. Intégration libevent / libnghttp2 / libsysrepo / libjwt

**Items liés :** A1 à A21
**RFC liées :** indirectement toutes

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-IMPL-01 | Appels sysrepo bloquants | Exécutés hors du thread de boucle libevent | ⬜ À faire |
| T-IMPL-02 | Callbacks de notification sysrepo | Handoff thread-safe vers libevent | ⬜ À faire |
| T-IMPL-03 | Sessions sysrepo par identité | Les sessions sont correctement associées à l’utilisateur JWT | ⬜ À faire |
| T-IMPL-04 | Génération ETag/Last-Modified | Mécanisme local cohérent | ⬜ À faire |
| T-IMPL-05 | Décodage API path | Aucun XPath construit à partir d’un chemin brut non décodé | ⬜ À faire |
| T-IMPL-06 | Validation JWT par thread | Pas de partage non sûr d’un contexte `jwt_t` | ⬜ À faire |
| T-IMPL-07 | Fetch JWKS | Cache, rotation et erreurs réseau gérés | ⬜ À faire |
| T-IMPL-08 | Formatteur d’erreurs centralisé | Toutes les erreurs passent par la même fonction/table | ⬜ À faire |
| T-IMPL-09 | Formatteur YANG Patch | Séparé du formatteur `ietf-restconf:errors` | ⬜ À faire |
| T-IMPL-10 | Nettoyage des souscriptions | Aucune fuite mémoire ou état orphelin après fermeture | ⬜ À faire |
| T-IMPL-11 | `content-id` YANG Library | Mis à jour lors des changements de modules | ⬜ À faire |
| T-IMPL-12 | Négociation de contenu | Centralisée et cohérente pour données et erreurs | ⬜ À faire |
| T-IMPL-13 | OPTIONS/Allow | Méthodes autorisées centralisées par type de ressource | ⬜ À faire |
| T-IMPL-14 | SSE sur HTTP/2 | Reprise après WINDOW_UPDATE | ⬜ À faire |
| T-IMPL-15 | Arrêt propre du serveur | Fermeture des sessions, streams et souscriptions | ⬜ À faire |

---

### 22. Tests d’interopérabilité

**Items liés :** tous
**RFC liées :** toutes

| ID | Test | Résultat attendu | Avancement |
|---|---|---|---|
| T-INTEROP-01 | Client RESTCONF générique en JSON | Opérations CRUD fonctionnelles | ⬜ À faire |
| T-INTEROP-02 | Client RESTCONF générique en XML | Opérations CRUD fonctionnelles | ⬜ À faire |
| T-INTEROP-03 | Client avec HTTP/2 | Connexion et opérations correctes | ⬜ À faire |
| T-INTEROP-04 | Client avec SSE | Réception des notifications | ⬜ À faire |
| T-INTEROP-05 | Client avec souscriptions dynamiques | Établissement, réception, suppression | ⬜ À faire |
| T-INTEROP-06 | Client avec YANG Patch | Éditions multiples fonctionnelles | ⬜ À faire |
| T-INTEROP-07 | Client avec NMDA | Lecture/écriture sur datastores supportés | ⬜ À faire |
| T-INTEROP-08 | Client avec Call Home | Connexion sortante acceptée et exploitée | ⬜ À faire |
| T-INTEROP-09 | Client avec JWT Bearer | Authentification acceptée | ⬜ À faire |
| T-INTEROP-10 | Client avec token expiré | Rejet propre | ⬜ À faire |
| T-INTEROP-11 | Outils CLI type `curl`, `nghttp` | Requêtes de base fonctionnelles | ⬜ À faire |
| T-INTEROP-12 | Outils de validation YANG type `yanglint` | Sorties JSON/XML validables | ⬜ À faire |

---

### 23. Critères globaux d’acceptation

Une implémentation peut être considérée comme prête pour une revue de conformité lorsque :

1. tous les tests obligatoires du cœur RESTCONF passent ;
2. les erreurs sont toujours retournées dans le format `ietf-restconf:errors` ou `yang-patch-status` selon le cas ;
3. les headers `WWW-Authenticate` et `Allow` sont présents lorsque requis ;
4. aucune donnée protégée par NACM n’est exposée à un utilisateur non autorisé ;
5. les flux SSE restent stables derrière le reverse proxy ;
6. les souscriptions sont correctement nettoyées après fermeture client ;
7. les validateurs conditionnels sont cohérents après modification des données ;
8. la YANG Library reflète exactement l’état des modules chargés ;
9. les capacités annoncées correspondent aux fonctionnalités réellement actives ;
10. les tests JWT refusent les tokens invalides, expirés, mal signés ou mal destinés ;
11. les limites de sécurité empêchent les abus par multiplexing HTTP/2 ;
12. les tests NMDA, YANG Patch, subscriptions et YANG-Push passent si ces fonctionnalités sont déclarées supportées.

---

## Notes de structure

- Les IDs représentent un ordre logique d’implémentation ; les colonnes `Dépendances` indiquent les points à finaliser avant de démarrer.
- R1 à R25 couvrent le cœur du protocole défini par RFC 8040.
- R6 à R9 découpent la méthode GET.
- R10/R11 découpent POST en mode création et mode invocation.
- R23/R24/R25 découpent la ressource de flux d’événements (SSE).
- R26 à R28 couvrent des extensions normalisées : YANG Patch et NMDA.
- R29 est transversal : toutes les opérations manipulant des données, des RPC, des actions, des notifications ou des souscriptions doivent appliquer NACM.
- R30 à R39 concernent les fonctionnalités avancées : Call Home, souscriptions, YANG-Push, binding RESTCONF des souscriptions.
- R40 à R44 ajoutent les exigences transverses HTTP, media types, OPTIONS, parsing URI et durcissement sécurité.
- R45 à R51 ajoutent les précisions de conformité : capacités RESTCONF, mapping NMDA, souscriptions configurées, notification YANG Library, sécurité des URI de souscription, cache HTTP et tests.
- Le media type `application/yang-patch+json` / `application/yang-patch+xml` (RFC 8072) est indépendant du PATCH “plain” de RFC 8040.
- Le contrôle d’accès NACM doit être conçu comme une couche transverse, pas seulement comme une dépendance de R8.