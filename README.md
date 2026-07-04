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
| Parametres non supportes | toutes methodes | Rejet explicite `400 invalid-value` au lieu d'une ignorance silencieuse |
| Negociation `Accept` / `Content-Type` | GET/HEAD/POST/PUT/PATCH | JSON RESTCONF uniquement : `application/yang-data+json` |
| `OPTIONS` / en-tete `Allow` | OPTIONS + erreurs 405 | OK pour les ressources RESTCONF exposees |
| `{+restconf}/operations/<module>:<rpc>` (RPC de haut niveau, RFC 8040 SS3.6) | POST | OK (voir "RPC" ci-dessous) |
| Actions YANG (`action`, invoquees sous `{+restconf}/data/...`/`{+restconf}/ds/<n>/...`, RFC 8040 SS3.6) | POST | OK sous `{+restconf}/ds/ietf-datastores:operational` uniquement (RFC 8527 SS3.1) ; `400 invalid-value` ailleurs (voir "RPC" ci-dessous) |
| Tout le reste (notifications SSE, XML, `fields`, `with-defaults`, `with-origin`, `insert`/`point`, ETag/Last-Modified, NACM/authn, remplacement complet de la datastore) | — | **Non implemente**, cf. "Feuille de route" |

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

`OPTIONS` renvoie `204 No Content` avec un en-tete `Allow` adapte a la ressource ciblee. Les
erreurs `405 operation-not-supported` ajoutent aussi `Allow`, ce qui facilite la decouverte par les
clients sans lancer d'operation sysrepo.

Les query parameters sont valides avant execution : les lectures de donnees acceptent seulement
`content` et `depth`; les autres ressources/methodes n'acceptent aucun parametre pour l'instant.
Ainsi `fields`, `with-defaults`, `with-origin`, `insert`, `point`, etc. renvoient `400
invalid-value` tant que leur semantique n'est pas implementee.

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

## RPC

Implemente dans `sysrepo_backend_rpc_invoke()` (`src/sysrepo_backend.c`), aiguille depuis
`handle_operation_invoke()` (`src/restconf_handler.c`) :

- Seules les operations RPC de haut niveau (statement YANG `rpc`) sont gerees, via
  `POST {+restconf}/operations/<module>:<rpc-name>` (RFC 8040 SS3.6/SS4.4.2). Le chemin est
  d'abord verifie contre le schema compile libyang (`lys_find_path()` + `nodetype == LYS_RPC`)
  pour rejeter proprement (`400 invalid-value`) tout chemin qui ne designe pas une operation RPC
  connue (module inconnu, RPC inexistant, chemin a plusieurs segments ou avec cles).
- Les **actions** (statement YANG `action`, invoquees via `POST {+restconf}/data/<...>/<action>`
  ou `POST {+restconf}/ds/<n>/<...>/<action>`, RFC 8040 SS3.6) sont routees separement des
  ressources de donnees ordinaires : `sysrepo_backend_is_action_path()` resout le dernier segment du
  chemin dans le schema compile libyang (fonction dediee `resolve_schema_node()`, distincte de
  `build_xpath()` qui construit un chemin de DONNEES) et verifie qu'il s'agit d'un noeud
  `LYS_ACTION` plutot que d'un noeud de donnees ordinaire ; `restconf_path_parse()` lui-meme n'a pas
  besoin d'etre modifie, un chemin d'action ayant exactement la meme forme syntaxique qu'un chemin
  de donnees. `restconf_handler.c` aiguille alors le POST vers `handle_action_invoke()` ->
  `sysrepo_backend_action_invoke()`, qui reutilise `sr_rpc_send_tree()` avec l'arbre COMPLET
  (ancetres + cles inclus) jusqu'a l'instance de l'action, contrairement a un RPC de haut niveau ou
  seul le noeud `rpc` est soumis. Conformement a la RFC 8527 SS3.1 ("YANG actions can only be
  invoked in `{+restconf}/ds/ietf-datastores:operational`"), cette invocation n'est autorisee que
  sous `{+restconf}/ds/ietf-datastores:operational` ; ailleurs (notamment `{+restconf}/data`, qui
  correspond a `running` dans ce squelette), elle est rejetee en `400 invalid-value`.
- Le corps de requete (RFC 8040 SS3.6.1, enveloppe JSON `"module:input"`) est analyse par
  `lyd_parse_data()` directement comme enfant du noeud RPC construit avec `lyd_new_path()` a partir
  du chemin de schema `/module:rpc-name` ; un corps absent est tolere (certains RPC n'ont pas de
  section `input`, ou une section `input` sans noeud mandatoire).
- L'invocation elle-meme passe par `sr_rpc_send_tree()`. La sortie (RFC 8040 SS3.6.2, section
  `output`) est recherchee explicitement dans l'arbre renvoye (noeud de schema `LYS_OUTPUT`) plutot
  que de supposer sa position, par prudence analogue aux marqueurs `XXX-LY-API`/`XXX-SR-API`
  ailleurs dans ce fichier -- **a verifier au premier test reel** contre votre sysrepo installe, cf.
  "Points sensibles a la version installee". Si `output` est vide ou absent, la reponse est
  `204 No Content` ; sinon `200 OK` avec le corps `{"module:output": {...}}`.
- Les erreurs sysrepo sont traduites en error-tag RESTCONF au mieux (`access-denied` pour
  `SR_ERR_UNAUTHORIZED`, `invalid-value` pour `SR_ERR_INVAL_ARG`, `operation-failed` sinon) ;
  `sr_strerror()` fournit le message.

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

3bis. **`src/sysrepo_backend.c` / `sysrepo_backend_rpc_invoke()`** : appelle `sr_rpc_send_tree()`
   avec le noeud RPC lui-meme (input attache comme enfant) et recupere l'arbre de sortie renvoye
   par cette meme fonction. Verifiez dans votre `sysrepo.h` l'ordre exact des parametres (unite du
   timeout notamment) et la structure precise de l'arbre `output` renvoye : le code recherche le
   noeud de schema `LYS_OUTPUT` explicitement plutot que de supposer sa position (racine ou
   enfant), mais cette hypothese meme n'a pas pu etre verifiee contre une sysrepo reelle ici.

Ces points n'ont **pas pu etre compiles/testes dans cet environnement** (pas d'acces reseau
pour installer sysrepo/libyang/fcgi2 ici) : relisez-les avant la premiere compilation.

## Modules YANG requis dans sysrepo

Au demarrage, `sysrepo_backend_init()` verifie que les modules indispensables sont deja charges
comme modules implementes dans le contexte sysrepo/libyang :

- `ietf-yang-library`
- `ietf-datastores`
- `ietf-restconf`
- `ietf-restconf-monitoring`
- `ietf-netconf`

Cette verification rend explicite une contrainte qui etait deja implicite : le serveur emet des
donnees `ietf-restconf:*`, annonce `ietf-restconf-monitoring:restconf-state`, manipule les
identityrefs `ietf-datastores:*` et pose la metadata d'edition `ietf-netconf:operation`.

Exemple d'installation, a adapter selon l'emplacement de votre depot `yang` et les dependances deja
presentes dans sysrepo :

```sh
sysrepoctl -i /path/to/yang/standard/ietf/RFC/ietf-datastores.yang
sysrepoctl -i /path/to/yang/standard/ietf/RFC/ietf-netconf.yang
sysrepoctl -i /path/to/yang/standard/ietf/RFC/ietf-yang-library.yang
sysrepoctl -i /path/to/yang/standard/ietf/RFC/ietf-restconf.yang
sysrepoctl -i /path/to/yang/standard/ietf/RFC/ietf-restconf-monitoring.yang
```

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
- **Plugin sysrepo** : Convertir l'implémentation de ietf-restconf-monitoring, ietf-restconf sous forme de plugin sysrepo.
- ~~**Ecritures** : POST (creation), PUT (remplacement), PATCH (fusion "plain patch"), DELETE~~ ->
  fait (voir "Ecritures" ci-dessus), a l'exception du remplacement/fusion complet de la datastore
  via PUT/PATCH directement sur `{+restconf}/data`/`{+restconf}/ds/<name>` (RFC 8040 SS4.5
  Appendix B.2.4), qui reste a faire (necessite de deballer l'enveloppe `ietf-restconf:data` du
  corps de requete).
- ~~**RPC** : POST sur `{+restconf}/operations/<module>:<rpc>` -> `sr_rpc_send_tree()`~~ -> fait
  (voir "RPC" ci-dessus).
- ~~**Actions** (`{+restconf}/data/<...>/<action>`, statement YANG `action` invoque sous une
  ressource de donnees)~~ -> fait : `sysrepo_backend_is_action_path()` distingue une action d'un
  noeud de donnees ordinaire (dernier segment de type `LYS_ACTION` dans le schema compile, sans
  modifier `restconf_path_parse()`) et `sysrepo_backend_action_invoke()` reutilise
  `sr_rpc_send_tree()` avec l'arbre complet (ancetres + cles) jusqu'a l'action. Cote
  `restconf_handler.c`, un POST sur une ressource de donnees dont le dernier segment est une action
  est aiguille vers `handle_action_invoke()` au lieu de `handle_data_post()` ; conformement a la RFC
  8527 SS3.1 ("YANG actions can only be invoked in `{+restconf}/ds/ietf-datastores:operational`"),
  ceci n'est autorise que sous `{+restconf}/ds/ietf-datastores:operational` (`{+restconf}/data`, qui
  correspond a `running` dans ce squelette, renvoie `400 invalid-value` pour une action). Non teste
  contre un sysrepo reel, cf. "Points sensibles a la version installee".
- **Notifications** : flux SSE sur `text/event-stream` -> `sr_event_notif_subscribe_tree()`: ~5-8 jours
- **Parametres de requete restants** : ~~`depth`~~, `fields`, `with-defaults` (RFC 8040 SS4.8.9 et
  son cas particulier operationnel RFC 8527 SS3.2.1), `with-origin` (RFC 8527 SS3.2.2), `insert`/
  `point`. `depth` est implemente pour GET/HEAD via le `max_depth` de `sr_get_data()`; les autres
  parametres non supportes sont maintenant rejetes explicitement en `400 invalid-value`.
- **ETag / Last-Modified** pour collision detection: ~1-2 jours (RFC 8040 SS3.4.1/3.5.1-2).
- **Authentification/autorisation** : TLS + NACM (`sr_session_set_user`, `SR_SESS_ENABLE_NACM`): ~6-10 jours et NACM
  cote sysrepo (`sr_session_set_user`/`SR_SESS_ENABLE_NACM` a etudier). l'authentification est géré par un programme externe. l'utilisateur fournie un cookie JWT (RFC7519, RFC8725 et RFC7797) contenant le nom de l'utilisateur. La clef de vérification du JWT est stocké dans le keyring kernel.   
- **Support XML** en plus de JSON: ~4-6 jours (encoder/decoder + Accept negotiation) `Accept`/`Content-Type` existe maintenant pour
  refuser proprement les representations non supportees, mais seul JSON est encode/decode pour
  l'instant. Une premiere ebauche (`lyd_to_xml_string`/`extract_xml_string`) avait ete inseree dans
  `src/sysrepo_backend.c` mais coupait en deux la definition de `sysrepo_backend_rpc_invoke`
  (fichier alors non compilable) et etait de toute facon incomplete (mauvaise gestion d'erreur,
  acces a un membre `tree->top` inexistant, `struct restconf_error` locale jamais remontee a
  l'appelant) ; elle a ete retiree. Ce chantier reste donc entierement a refaire proprement, avec
  ses propres fonctions statiques bien formees et une vraie remontee d'erreur via `struct
  restconf_error *err`.
- ~~**`restconf-state/capabilities`** (`ietf-restconf-monitoring`) pour annoncer les query
  parameters reellement supportes (RFC 8040 SS9)~~ -> fait avec une annonce conservative
  (`defaults` et `depth` pour l'instant).
- ~~**Support yang** : verifier au demarrage que sysrepo expose les modules IETF requis par le
  serveur RESTCONF~~ -> fait (`ietf-yang-library`, `ietf-datastores`, `ietf-restconf`,
  `ietf-restconf-monitoring`, `ietf-netconf`). L'installation automatique via `sysrepoctl` reste
  volontairement hors du daemon.
- **Librairie** : Créé une librairie Resconf/sysrepo indépendant du frontend Fastcgi2.
- **Frama-C** : Ajouter une instrumentation du code pour le prouver formellement avec Frama-C
- **Fuzzing** : Ajouter un programme de fuzzing avec AFL++ qui valide la librairie.

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
yang/                restconf yang schema
```
