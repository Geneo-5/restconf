# restconfd — serveur RESTCONF (RFC 8040 + RFC 8527) sur fastcgi2 + sysrepo

Squelette **phase 2 (lecture + ecriture de base)** d'un serveur RESTCONF s'appuyant sur :

- [fcgi2](https://github.com/FastCGI-Archives/fcgi2) (API `FCGX_*`) comme transport HTTP via un
  serveur web frontal (nginx, etc.) qui parle FastCGI ;
- [sysrepo](https://github.com/sysrepo/sysrepo) comme moteur de datastores YANG/NMDA, via son
  API C et libyang pour la (de)serialisation JSON (RFC 7951).
- [yang](https://github.com/YangModels/yang.git) IETF standards-track YANG modules

## Ce qui est implemente

| Ressource RESTCONF | Methode | Statut |
|---|---|---|
| `/.well-known/host-meta` | GET | OK |
| `{+restconf}` (ressource API) | GET | OK |
| `{+restconf}/yang-library-version` | GET | OK |
| `{+restconf}/data[/<api-path>]` | GET/HEAD | OK (voir hypothese ci-dessous) |
| `{+restconf}/data/ietf-restconf-monitoring:restconf-state/capabilities` | GET/HEAD | OK (annonce conservative, voir "Monitoring") |
| `{+restconf}/ds/<datastore>[/<api-path>]` (RFC 8527) | GET/HEAD | OK pour `running`, `candidate`, `startup`, `operational` |
| `{+restconf}/data[/<api-path>]` et `{+restconf}/ds/<name>/<api-path>` | POST (creation) | OK (voir "Ecritures" ci-dessous) ; pas sur `operational` (lecture seule) |
| idem | PUT (remplacement) | OK sur une ressource de donnees ; PUT sur la racine de la datastore (remplacement complet) non implemente |
| idem | PATCH ("plain patch", fusion) | OK sur une ressource de donnees ; PATCH sur la racine de la datastore non implemente |
| idem | DELETE | OK sur une ressource de donnees ; non defini sur la racine de la datastore (RFC 8040 SS4.7) |
| Parametre `content` | GET | Partiel (voir plus bas) |
| Parametre `depth` | GET/HEAD | OK (`unbounded` ou entier positif, via `sr_get_data(..., max_depth, ...)`) |
| Negociation `Accept` / `Content-Type` | GET/HEAD/POST/PUT/PATCH | JSON RESTCONF uniquement : `application/yang-data+json` |
| Tout le reste (RPC/actions, notifications SSE, XML, `fields`, `with-defaults`, `with-origin`, `insert`/`point`, ETag/Last-Modified, NACM/authn, remplacement complet de la datastore) | — | **Non implemente**, cf. "Feuille de route" |

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

La negociation de contenu est volontairement minimale : les reponses RESTCONF JSON exigent un
`Accept` absent, compatible avec `application/yang-data+json` (ou un joker HTTP), sinon le serveur
renvoie `406`. Les requetes d'ecriture `POST`/`PUT`/`PATCH` exigent
`Content-Type: application/yang-data+json`, sinon elles renvoient `415`. Le support XML
(`application/yang-data+xml`) reste a implementer.

## Ecritures (POST/PUT/PATCH/DELETE)

Implementees dans `sysrepo_backend_write()`/`sysrepo_backend_delete()` (`src/sysrepo_backend.c`) :

- Le corps JSON de la requete est analyse avec `lyd_parse_data()` en le rattachant directement au
  noeud parent concerne dans l'arbre libyang (construit au prealable avec `lyd_new_path2()` a
  partir du chemin RESTCONF), ce qui fait porter la validation du schema (module/nom/type
  attendus a cet endroit) a libyang lui-meme plutot qu'a du code de correspondance ad hoc.
- Chaque noeud ainsi apporte par le corps est marque avec la metadata d'edition NETCONF standard
  (`ietf-netconf:operation` = `create` pour POST, `replace` pour PUT, `merge` pour PATCH) avant
  d'etre soumis via `sr_edit_batch()` (operation par defaut `merge`, sans effet sur le squelette
  d'ancetres du chemin) puis `sr_apply_changes()`. C'est cette metadata qui fait qu'un POST sur une
  ressource deja existante echoue avec `data-exists` (409) au lieu de silencieusement la remplacer.
- PATCH exige que la ressource **parente** existe deja (sinon `data-missing`, 409) ; PUT determine
  si la ressource **cible** existe deja (sonde `sr_get_subtree` avant edition) pour repondre 201 ou
  204 conformement a la RFC 8040 SS4.5.
- L'en-tete `Location` d'un POST reussi (RFC 8040 SS4.4.1, MUST) est reconstruit a partir du chemin
  de la requete et d'un segment `module:nom` (ou `module:nom=cle1,cle2` pour une liste) derive du
  noeud fraichement analyse ; les valeurs de cle sont percent-encodees par prudence mais sans
  gestion fine de tous les caracteres reserves de la RFC 8040 SS3.5.3 (a completer si vos cles
  utilisent des caracteres inhabituels).
- **Non gere** : remplacement/fusion de la datastore entiere via PUT/PATCH directement sur
  `{+restconf}/data` ou `{+restconf}/ds/<name>` (RFC 8040 SS4.5, exemple Appendix B.2.4, qui
  s'appuie sur l'enveloppe `data` du module `ietf-restconf`) ; DELETE n'est pas defini sur cette
  meme racine (RFC 8040 SS4.7). Ces deux cas renvoient actuellement `501`/`400`.
- L'ecriture est bloquee sur la datastore `operational` par le meme garde-fou que la lecture (cf.
  "Hypotheses de conception" ci-dessus : `read_only` dans `DS_MAP`), coherent avec le fait que
  sysrepo peuple cette datastore via des abonnements plutot que des edits RESTCONF classiques.

## Monitoring RESTCONF

`src/restconf_handler.c` intercepte maintenant localement :

- `{+restconf}/data/ietf-restconf-monitoring:restconf-state`
- `{+restconf}/data/ietf-restconf-monitoring:restconf-state/capabilities`
- `{+restconf}/data/ietf-restconf-monitoring:restconf-state/capabilities/capability`

Ces ressources exposent le conteneur obligatoire `restconf-state/capabilities` du module
`ietf-restconf-monitoring` (RFC 8040 SS9.1). L'annonce est volontairement conservative :
`urn:ietf:params:restconf:capability:defaults:1.0?basic-mode=explicit` et
`urn:ietf:params:restconf:capability:depth:1.0` sont publies pour l'instant. Les capacites
optionnelles liees a `fields`, `with-defaults`, `filter`, `start-time`, `stop-time`, etc. ne
seront ajoutees que lorsque les parametres correspondants seront reellement implementes.

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
3. **`src/sysrepo_backend.c` / `sysrepo_backend_write()`** : construit un squelette d'ancetres avec
   `lyd_new_path2()` (9 parametres dont `value_size_bits`/`any_hints` separes dans les versions
   recentes de libyang) puis y rattache le corps JSON de la requete avec `lyd_parse_data()` a
   *parent* explicite. Le comportement exact du noeud `new_node` renvoye par `lyd_new_path2()`
   pour un chemin ciblant une liste a varie entre versions de libyang 2.x (cf. discussion amont
   CESNET/libyang#2337) ; le code re-resout donc ce noeud via `lyd_find_path()` par securite, mais
   verifiez ces signatures dans votre `libyang/tree_data.h`. De meme, `lyd_new_attr()` (pose de la
   metadata `ietf-netconf:operation` consommee par `sr_edit_batch()`) suppose que le module
   `ietf-netconf` est resolvable dans le contexte libyang de sysrepo.

Ces trois points n'ont **pas pu etre compiles/testes dans cet environnement** (pas d'acces reseau
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

- ~~**Ecritures** : POST (creation), PUT (remplacement), PATCH (fusion "plain patch"), DELETE~~ ->
  fait (voir "Ecritures" ci-dessus), a l'exception du remplacement/fusion complet de la datastore
  via PUT/PATCH directement sur `{+restconf}/data`/`{+restconf}/ds/<name>` (RFC 8040 SS4.5
  Appendix B.2.4), qui reste a faire (necessite de deballer l'enveloppe `ietf-restconf:data` du
  corps de requete).
- **RPC/actions** : POST sur `{+restconf}/operations/<op>` -> `sr_rpc_send_tree()`.
- **Notifications** : flux SSE sur `text/event-stream` -> `sr_event_notif_subscribe_tree()`.
- **Parametres de requete restants** : ~~`depth`~~, `fields`, `with-defaults` (RFC 8040 SS4.8.9 et
  son cas particulier operationnel RFC 8527 SS3.2.1), `with-origin` (RFC 8527 SS3.2.2), `insert`/
  `point`. `depth` est implemente pour GET/HEAD via le `max_depth` de `sr_get_data()`.
- **ETag / Last-Modified** pour la detection de collision d'edition (RFC 8040 SS3.4.1/3.5.1-2).
- **Authentification/autorisation** : TLS + identite client (deleguee a nginx en frontal) et NACM
  cote sysrepo (`sr_session_set_user`/`SR_SESS_ENABLE_NACM` a etudier).
- **Support XML** en plus de JSON. La negociation `Accept`/`Content-Type` existe maintenant pour
  refuser proprement les representations non supportees, mais seul JSON est encode/decode pour
  l'instant.
- ~~**`restconf-state/capabilities`** (`ietf-restconf-monitoring`) pour annoncer les query
  parameters reellement supportes (RFC 8040 SS9)~~ -> fait avec une annonce conservative
  (`defaults` et `depth` pour l'instant).
- **Support yang** : s'enregistre au pres de sysrepo comme service implémentant ietf restconf yang modules

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
