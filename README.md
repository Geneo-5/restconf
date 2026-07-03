# restconfd — serveur RESTCONF (RFC 8040 + RFC 8527) sur fastcgi2 + sysrepo

Squelette **phase 1 (lecture seule)** d'un serveur RESTCONF s'appuyant sur :

- [fcgi2](https://github.com/FastCGI-Archives/fcgi2) (API `FCGX_*`) comme transport HTTP via un
  serveur web frontal (nginx, etc.) qui parle FastCGI ;
- [sysrepo](https://github.com/sysrepo/sysrepo) comme moteur de datastores YANG/NMDA, via son
  API C et libyang pour la (de)serialisation JSON (RFC 7951).

## Ce qui est implemente

| Ressource RESTCONF | Methode | Statut |
|---|---|---|
| `/.well-known/host-meta` | GET | OK |
| `{+restconf}` (ressource API) | GET | OK |
| `{+restconf}/yang-library-version` | GET | OK |
| `{+restconf}/data[/<api-path>]` | GET/HEAD | OK (voir hypothese ci-dessous) |
| `{+restconf}/ds/<datastore>[/<api-path>]` (RFC 8527) | GET/HEAD | OK pour `running`, `candidate`, `startup`, `operational` |
| Parametre `content` | GET | Partiel (voir plus bas) |
| Tout le reste (POST/PUT/PATCH/DELETE, RPC/actions, notifications SSE, XML, `depth`, `fields`, `with-defaults`, `with-origin`, `insert`/`point`, ETag/Last-Modified, NACM/authn) | — | **Non implemente**, cf. "Feuille de route" |

## Hypotheses de conception a valider avec vous

Le mapping de `{+restconf}/data` (ressource pre-NMDA, combinee config+etat selon la RFC 8040) sur
un datastore sysrepo precis n'est pas entierement dicte par la RFC. Ce squelette fait le choix
simple suivant, **a ajuster selon votre cas d'usage** :

- `{+restconf}/data` -> datastore sysrepo `running` (configuration uniquement).
- `{+restconf}/ds/ietf-datastores:running` -> `SR_DS_RUNNING`
- `{+restconf}/ds/ietf-datastores:candidate` -> `SR_DS_CANDIDATE`
- `{+restconf}/ds/ietf-datastores:startup` -> `SR_DS_STARTUP`
- `{+restconf}/ds/ietf-datastores:operational` -> `SR_DS_OPERATIONAL`, marquee **lecture seule**
  cote RESTCONF dans ce squelette (sysrepo peuple `operational` via les abonnements
  `sr_oper_get_items_subscribe`/push oper data, pas via des PUT/PATCH RESTCONF classiques — a
  revoir si vous voulez exposer l'ecriture de donnees operationnelles "poussees").
- `{+restconf}/ds/ietf-datastores:intended` : non expose (pas d'equivalent direct dans sysrepo).

Le parametre `content=config|nonconfig` n'est reellement filtre que pour la datastore
`operational` (via les indicateurs `SR_OPER_NO_STATE`/`SR_OPER_NO_CONFIG` — **verifiez ces noms de
constantes dans votre `sysrepo.h` installe**, ils different selon la version). Pour
running/candidate/startup (purement configuration chez sysrepo), le filtre est ignore.

La negociation de contenu (`Accept`) et le support XML ne sont pas geres : le serveur repond
toujours en `application/yang-data+json`, ce qui est conforme a la RFC (un serveur DOIT supporter
au moins un des deux formats) mais incomplet pour des clients qui exigeraient du XML.

## Points sensibles a la version installee (a verifier avant de compiler)

Deux zones du code sont explicitement commentees `XXX-SR-API` / `XXX-LY-API` car les API
publiques de sysrepo et libyang ont evolue :

1. **`src/sysrepo_backend.c` / `fetch_tree()`** : cible l'API sysrepo >= 2.x ou `sr_get_data()` et
   `sr_get_subtree()` renvoient un `sr_data_t *` (libere par `sr_release_data()`). Sur une
   sysrepo 1.x, ces fonctions renvoient directement un `struct lyd_node **` (libere autrement).
   Comparez avec le `sysrepo.h` que vous avez effectivement compile (depot indique dans le
   fichier projet `sysrepo`).
2. **`src/sysrepo_backend.c` / `build_xpath()`** : la resolution des noms de cles de liste
   (le chemin RESTCONF ne transporte que les *valeurs*, dans l'ordre du `key` YANG — RFC 8040
   SS3.5.3) parcourt le schema compile libyang via `lysc_node_children()` et le drapeau
   `LYS_KEY`. Verifiez que ces symboles existent tels quels dans votre
   `libyang/tree_schema.h`.

Ces deux points n'ont **pas pu etre compiles/testes dans cet environnement** (pas d'acces reseau
pour installer sysrepo/libyang/fcgi2 ici) : relisez-les avant la premiere compilation.

## Construction

```sh
# 1. Compiler/installer libyang, sysrepo (depot indique dans le fichier projet "sysrepo")
#    et fcgi2 (depot indique dans le fichier projet "fastcgi2") au prealable.

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j"$(nproc)"
```

Si les libs ne sont pas dans des emplacements standards :

```sh
cmake .. -DSYSREPO_INCLUDE_DIR=/opt/sr/include -DSYSREPO_LIBRARY=/opt/sr/lib/libsysrepo.so \
         -DLIBYANG_INCLUDE_DIR=/opt/sr/include -DLIBYANG_LIBRARY=/opt/sr/lib/libyang.so \
         -DFCGI_INCLUDE_DIR=/opt/fcgi2/include -DFCGI_LIBRARY=/opt/fcgi2/lib/libfcgi.so
```

## Lancement (mode socket Unix, pour test manuel derriere nginx)

```sh
./restconfd /run/restconfd.sock 4 /restconf
```

- 1er argument : chemin du socket Unix (ou `:9000` pour TCP) sur lequel `FCGX_OpenSocket` ecoute.
  Sans argument, le processus suppose etre lance par le serveur web via le fd 0 (mode
  "spawn-fcgi"/systemd socket-activation classique).
- 2e argument : nombre de threads travailleurs (defaut 4).
- 3e argument : racine `{+restconf}` (defaut `/restconf`).

### Exemple de configuration nginx

Voir `etc/nginx-restconf.conf.example`. Point important : `fastcgi_split_path_info` doit isoler
`/restconf` dans `SCRIPT_NAME` et le reste dans `PATH_INFO`, c'est ce que lit `http.c`.

### Test rapide

```sh
curl -s http://localhost/.well-known/host-meta
curl -s http://localhost/restconf | jq
curl -s http://localhost/restconf/data/ietf-yang-library:yang-library | jq
curl -s http://localhost/restconf/ds/ietf-datastores:operational | jq
```

## Feuille de route (phase 2 et suivantes, a prioriser ensemble)

- **Ecritures** : POST (creation), PUT (remplacement), PATCH (fusion "plain patch"), DELETE, en
  s'appuyant sur `sr_edit_batch()`/`sr_set_item_str()`/`sr_delete_item()` + `sr_apply_changes()`,
  avec le mapping d'erreurs `data-exists` (409), `data-missing` (409), `lock-denied` (409), etc.
- **RPC/actions** : POST sur `{+restconf}/operations/<op>` -> `sr_rpc_send_tree()`.
- **Notifications** : flux SSE sur `text/event-stream` -> `sr_event_notif_subscribe_tree()`.
- **Parametres de requete restants** : `depth`, `fields`, `with-defaults` (RFC 8040 SS4.8.9 et son
  cas particulier operationnel RFC 8527 SS3.2.1), `with-origin` (RFC 8527 SS3.2.2), `insert`/
  `point`.
- **ETag / Last-Modified** pour la detection de collision d'edition (RFC 8040 SS3.4.1/3.5.1-2).
- **Authentification/autorisation** : TLS + identite client (deleguee a nginx en frontal) et NACM
  cote sysrepo (`sr_session_set_user`/`SR_SESS_ENABLE_NACM` a etudier).
- **Support XML** en plus de JSON, negociation via `Accept`/`Content-Type`.
- **`restconf-state/capabilities`** (`ietf-restconf-monitoring`) pour annoncer les query
  parameters reellement supportes (RFC 8040 SS9).

## Arborescence

```
CMakeLists.txt
include/            en-tetes publics des modules
src/
  main.c             boucle FCGX_Accept_r multi-threads
  http.c             extraction FCGX_Request -> http_request / ecriture http_response
  restconf_path.c     analyse syntaxique des chemins RESTCONF (api-path RFC 8040 SS3.5.3.1)
  errors.c            modele d'erreur ietf-restconf:errors (RFC 8040 SS7-8)
  sysrepo_backend.c    connexion sysrepo, mapping datastores RFC 8527, lecture -> JSON
  restconf_handler.c   dispatcher HTTP -> sysrepo selon le type de ressource
etc/
  nginx-restconf.conf.example
```
