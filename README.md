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
| `{+restconf}/data/ietf-restconf-monitoring:restconf-state/capabilities` | GET/HEAD | OK (voir "Monitoring RESTCONF") |
| `{+restconf}/ds/<datastore>[/<api-path>]` (RFC 8527) | GET/HEAD | OK pour `running`, `candidate`, `startup`, `operational` |
| `{+restconf}/data[/<api-path>]` et `{+restconf}/ds/<name>/<api-path>` | POST (creation) | OK (voir "Ecritures" ci-dessous) ; pas sur `operational` (lecture seule) |
| idem | PUT (remplacement) | OK, y compris PUT sur la racine de la datastore (remplacement complet, RFC 8040 SS4.5 Appendix B.2.4, voir "Ecritures") |
| idem | PATCH ("plain patch", fusion) | OK, y compris PATCH sur la racine de la datastore (fusion complete, RFC 8040 SS4.6.1 exemple B.2.3, voir "Ecritures") |
| idem | DELETE | OK sur une ressource de donnees ; non defini sur la racine de la datastore (RFC 8040 SS4.7) |
| Parametre `content` | GET | Partiel (voir plus bas) |
| Parametre `depth` | GET/HEAD | OK (`unbounded` ou entier positif, via `sr_get_data(..., max_depth, ...)`) |
| Parametre `fields` | GET/HEAD | OK (grammaire complete, voir "Parametres de requete" ci-dessous) |
| Parametre `with-defaults` | GET/HEAD | OK (les 4 valeurs RFC 8040 SS4.8.9, voir "Parametres de requete" ci-dessous) |
| Parametre `with-origin` (RFC 8527 SS3.2.2) | GET/HEAD | OK, uniquement sur `{+restconf}/ds/ietf-datastores:operational` |
| Parametres non supportes (`insert`, `point`, `filter`, `start-time`, `stop-time`) | toutes methodes | Rejet explicite `400 invalid-value` au lieu d'une ignorance silencieuse |
| Negociation `Accept` / `Content-Type` | GET/HEAD/POST/PUT/PATCH | JSON RESTCONF uniquement : `application/yang-data+json` |
| `OPTIONS` / en-tete `Allow` | OPTIONS + erreurs 405 | OK pour les ressources RESTCONF exposees |
| `{+restconf}/operations/<module>:<rpc>` (RPC de haut niveau, RFC 8040 SS3.6) | POST | OK (voir "RPC" ci-dessous) |
| Actions YANG (`action`, invoquees sous `{+restconf}/data/...`/`{+restconf}/ds/<n>/...`, RFC 8040 SS3.6) | POST | OK sous `{+restconf}/ds/ietf-datastores:operational` uniquement (RFC 8527 SS3.1) ; `400 invalid-value` ailleurs (voir "RPC" ci-dessous) |
| `ETag` / `Last-Modified` (RFC 8040 SS3.4.1) + `If-Match` / `If-Unmodified-Since` (detection de collision) | GET/HEAD (en-tetes de reponse) ; POST/PUT/PATCH/DELETE (preconditions + en-tetes de reponse) | OK au niveau de la datastore entiere (voir "ETag / Last-Modified" ci-dessous) |
| Tout le reste (notifications SSE, XML, NACM/authn) | — | **Non implemente**, cf. "Feuille de route" |

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

Les query parameters sont valides avant execution : les lectures de donnees (GET/HEAD sur
`{+restconf}/data` et `{+restconf}/ds/<name>`) acceptent `content`, `depth`, `fields`,
`with-defaults` et `with-origin` (ce dernier uniquement sur la datastore `operational`, sinon `400
invalid-value` ; cf. `parse_get_options()`) ; les autres ressources/methodes n'acceptent aucun
parametre pour l'instant. `insert`/`point`, `filter`, `start-time`, `stop-time` renvoient toujours
`400 invalid-value`, leur semantique n'etant pas implementee.

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
- **Remplacement/fusion de la datastore entiere** via PUT/PATCH directement sur `{+restconf}/data`
  ou `{+restconf}/ds/<name>` (RFC 8040 SS4.5 exemple Appendix B.2.4 pour PUT, SS4.6.1 exemple
  Appendix B.2.3 pour PATCH) : gere par `sysrepo_backend_write_datastore()`
  (`src/sysrepo_backend.c`), aiguille depuis `sysrepo_backend_write()` des que `nsegments == 0` et
  que l'operation n'est pas une creation. Le corps JSON attendu est l'enveloppe complete
  `{"ietf-restconf:data": {...}}` (le conteneur `data` n'est qu'un template YANG `yang-data`, RFC
  8040 SS8 : il n'existe pas comme noeud de schema reel, d'ou un petit deballage JSON minimal
  (`extract_data_envelope()`) avant de repasser la main a `lyd_parse_data()`). Pour PUT
  (remplacement complet), tout noeud de premier niveau present avant l'operation mais absent du
  nouveau corps est supprime explicitement (`sr_delete_item()`) ; les noeuds de premier niveau
  presents dans les deux arbres sont marques `ietf-netconf:operation="replace"`, ce qui fait
  supprimer recursivement, cote sysrepo, tout descendant absent du nouveau corps. Pour PATCH
  ("plain patch"), aucune suppression n'est effectuee : les noeuds du corps sont simplement
  fusionnes (`merge`), conformement a la semantique "plain patch". Cote
  `src/restconf_handler.c`, `handle_data_put()`/`handle_data_patch()` acceptent maintenant
  `path->nsegments == 0` (auparavant rejete en `501`/`400`) ; la reponse est toujours `204 No
  Content` pour ce cas (la ressource datastore elle-meme existe toujours, ce PUT/PATCH ne peut donc
  jamais la "creer"). DELETE reste non defini sur cette meme racine (RFC 8040 SS4.7, `405
  operation-not-supported`). Non teste contre un sysrepo/libyang reels (cf. "Points sensibles a la
  version installee") : la strategie de diff "premier niveau + replace recursif" est une hypothese
  de conception a valider.
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

## ETag / Last-Modified

Implemente dans `sysrepo_backend.c` (etat + logique de comparaison) et cable dans
`restconf_handler.c` (en-tetes de reponse + rejet des requetes) :

- **Granularite** : conformement a la RFC 8040 SS3.5.1 ("If not maintained, then the resource
  timestamp for the datastore MUST be used instead") et SS3.5.2 (idem pour l'entity-tag), ce
  squelette ne suit un couple `(compteur ETag, timestamp Last-Modified)` qu'au niveau de **chaque
  datastore sysrepo dans son ensemble** (`g_revisions[]`, indexe par `sr_datastore_t`, protege par
  un mutex, dans `sysrepo_backend.c`), pas par ressource de donnees individuelle. Une lecture de
  n'importe quelle ressource sous `{+restconf}/data` ou `{+restconf}/ds/<name>` renvoie donc les
  memes valeurs `ETag`/`Last-Modified` : celles de la datastore correspondante.
- **Etat en memoire uniquement** : ce compteur/timestamp est initialise paresseusement (a l'heure
  du premier acces) et n'est pas persiste — un redemarrage de `restconfd` reinitialise l'ETag et le
  Last-Modified de chaque datastore. Suffisant pour detecter des collisions entre clients RESTCONF
  concurrents pendant la duree de vie du processus, mais pas a travers un redemarrage ni des
  modifications faites hors RESTCONF (CLI sysrepo, NETCONF, etc.) — limitation a documenter cote
  operateurs si vous avez besoin d'une garantie plus forte.
- **Avancement** : `bump_datastore_revision()` est appelee apres tout POST/PUT/PATCH/DELETE reussi
  sur une ressource de configuration (y compris le remplacement/fusion complet de la datastore via
  `sysrepo_backend_write_datastore()`), qu'il s'agisse d'une ecriture "normale" (`sysrepo_backend_write()`)
  ou d'une suppression (`sysrepo_backend_delete()`). La datastore `operational`, en lecture seule
  dans ce squelette, n'est donc jamais avancee (ses valeurs restent celles de l'initialisation).
- **En-tetes de reponse** : `add_datastore_revision_headers()` (`restconf_handler.c`) ajoute `ETag`
  (valeur citee, ex. `"3"`) et `Last-Modified` (format `HTTP-date` RFC 7231 SS7.1.1.1) a toute
  reponse GET/HEAD sur une ressource de donnees (`handle_data_get()`), ainsi qu'aux reponses
  201/204 de POST/PUT/PATCH (RFC 8040 Appendix B.2.1/B.2.4/B.2.3).
- **Detection de collision** : `sysrepo_backend_check_preconditions()` compare les en-tetes de
  requete `If-Match` (RFC 7232 SS3.1, y compris `*` et les listes separees par des virgules) et
  `If-Unmodified-Since` (RFC 7232 SS3.4, format `HTTP-date` uniquement — les formats obsoletes RFC
  850/asctime ne sont pas geres, et une date non parsable est ignoree par prudence, "fail-open"
  conformement a l'esprit de la RFC 7232 SS3.4) a l'etat courant de la datastore ciblee. Cette
  verification est appelee (`check_write_preconditions()`) en tout debut de traitement de POST,
  PUT, PATCH et DELETE, **avant** toute ecriture cote sysrepo ; en cas d'echec, la requete est
  rejetee avec `412 Precondition Failed` (RFC 8040 Appendix B.2.2), accompagne des en-tetes
  `ETag`/`Last-Modified` courants (RFC 7232 SS4.2), sans corps de reponse. Si aucun des deux
  en-tetes n'est fourni par le client, l'ecriture procede normalement (comportement HTTP par
  defaut).
- **Limite connue** : la RFC 8040 SS3.4.1.3 decrit un modele plus fin ("changes to configuration
  data resources affect the timestamp and entity-tag for that resource, any ancestor data
  resources, and the datastore resource") qu'implemente ce squelette : un ETag/Last-Modified par
  ressource de donnees individuelle (pas seulement par datastore) permettrait une detection de
  collision plus precise (deux clients modifiant des sous-arbres disjoints ne se genent pas). Cette
  granularite plus fine n'est pas implementee ici, cf. "Feuille de route".

## Monitoring RESTCONF

`src/restconf_handler.c` intercepte maintenant localement :

- `{+restconf}/data/ietf-restconf-monitoring:restconf-state`
- `{+restconf}/data/ietf-restconf-monitoring:restconf-state/capabilities`
- `{+restconf}/data/ietf-restconf-monitoring:restconf-state/capabilities/capability`

Ces ressources exposent le conteneur obligatoire `restconf-state/capabilities` du module
`ietf-restconf-monitoring` (RFC 8040 SS9.1). Sont publiees, dans la mesure ou elles sont
reellement appliquees par `sysrepo_backend_get()` : `defaults` (RFC 8040 SS9.1.2,
`basic-mode=explicit`), `depth`, `fields`, `with-defaults` (RFC 8040 SS4.8.9) et,
specifiques a la NMDA, `with-operational-defaults`/`with-origin` (RFC 8527 SS3.2.1/SS3.2.2). Les
capacites liees a `filter`, `start-time`, `stop-time`, `insert`/`point` restent non annoncees
tant que ces parametres ne sont pas reellement implementes (cf. "Feuille de route").

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

4. **`src/sysrepo_backend.c` / `sysrepo_backend_write_datastore()`** (PUT/PATCH sur la racine
   d'une datastore) : utilise `lyd_path()` (`LYD_PATH_STD`) pour retrouver le chemin de donnees
   exact des noeuds de premier niveau a supprimer explicitement lors d'un remplacement complet
   (PUT). Verifiez cette signature (buffer fourni par l'appelant, valeur de retour) dans votre
   `libyang/tree_data.h` ; non teste contre un sysrepo/libyang reels.

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

## Conteneurisation (Docker)

Pour build et run dans un conteneur, consultez `docker/Dockerfile`.

```sh
docker build . -f docker/Dockerfile -t restconfd:latest
docker run -it --rm restconfd:latest /bin/bash
```

Pour lancer le serveur RESTCONF en mode socket Unix :

```sh
docker run -d \
   --name restconfd \
   --network host \
   -e FCGI_ENV=0 \
   -v /run/restconfd.sock:/dev/fd/0 \
   restconfd:latest "/bin/bash" && ./restconfd /dev/null 4 /restconf
```

Pour lancer le serveur RESTCONF en mode TCP (sur port 8000) :
```sh
docker run -d \
   --name restconfd \
   --network host \
   -e FCGI_ENV=0 \
   restconfd:latest && ./restconfd :9000 4 /restconf
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
  fait (voir "Ecritures" ci-dessus), y compris le remplacement/fusion complet de la datastore via
  PUT/PATCH directement sur `{+restconf}/data`/`{+restconf}/ds/<name>` (RFC 8040 SS4.5 Appendix
  B.2.4 / SS4.6.1 exemple B.2.3), via `sysrepo_backend_write_datastore()` qui deballe l'enveloppe
  `ietf-restconf:data` du corps de requete.
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
- ~~**Parametres de requete** : `depth`, `fields`, `with-defaults` (RFC 8040 SS4.8.9 et son cas
  particulier operationnel RFC 8527 SS3.2.1), `with-origin` (RFC 8527 SS3.2.2)~~ -> fait (voir
  "Parametres de requete" ci-dessus et `parse_get_options()`/`sysrepo_backend_get()`). Restent
  `insert`/`point` (RFC 8040 SS4.8.5/SS4.8.6, insertion dans une liste/leaf-list
  `ordered-by user`) : toujours rejetes explicitement en `400 invalid-value`, aucune semantique
  d'insertion implementee pour l'instant.
- ~~**ETag / Last-Modified** pour collision detection (RFC 8040 SS3.4.1/3.5.1-2)~~ -> fait (voir
  "ETag / Last-Modified" ci-dessus) : suivi au niveau de chaque datastore (pas par ressource de
  donnees individuelle, repli explicitement autorise par la RFC), en-tetes `ETag`/`Last-Modified`
  sur GET/HEAD et sur les reponses POST/PUT/PATCH reussies, verification `If-Match`/
  `If-Unmodified-Since` avant toute ecriture (POST/PUT/PATCH/DELETE) avec rejet `412 Precondition
  Failed`. Reste a affiner (granularite par ressource plutot que par datastore, RFC 8040 SS3.4.1.3)
  si vous en avez besoin.
- **Authentification/autorisation** : TLS + NACM (`sr_session_set_user`, `SR_SESS_ENABLE_NACM`): ~6-10 jours et NACM
  cote sysrepo (`sr_session_set_user`/`SR_SESS_ENABLE_NACM` a etudier). l'authentification est géré par un programme externe. l'utilisateur fournie un cookie JWT (RFC7519, RFC8725 et RFC7797) contenant le nom de l'utilisateur. La clef de vérification du JWT est stocké dans le keyring kernel.   
- **Support XML** en plus de JSON: ~4-6 jours (encoder/decoder + Accept negotiation) `Accept`/`Content-Type` existe maintenant pour
  refuser proprement les representations non supportees, mais seul JSON est encode/decode pour
  l'instant. Utiliser `LYD_JSON` ou `LYD_XML` pour les fonctions `lyd_parse_data` et `lyd_print_mem` en fonction de  `Accept`/`Content-Type`.
- ~~**`restconf-state/capabilities`** (`ietf-restconf-monitoring`) pour annoncer les query
  parameters reellement supportes (RFC 8040 SS9)~~ -> fait, mise a jour au fil de
  l'implementation des parametres (voir "Monitoring RESTCONF") : `defaults`, `depth`, `fields`,
  `with-defaults`, `with-operational-defaults`, `with-origin`.
- ~~**Support yang** : verifier au demarrage que sysrepo expose les modules IETF requis par le
  serveur RESTCONF~~ -> fait (`ietf-yang-library`, `ietf-datastores`, `ietf-restconf`,
  `ietf-restconf-monitoring`, `ietf-netconf`). L'installation automatique via `sysrepoctl` reste
  volontairement hors du daemon.
- **Librairie** : Créé une librairie Resconf/sysrepo indépendant du frontend Fastcgi2.
- **Frama-C** : Ajouter une instrumentation du code pour le prouver formellement avec Frama-C
- **Fuzzing** : Ajouter un programme de fuzzing avec AFL++ qui valide la librairie.
- **Test/Sample** : Créer des testes d'utilisation qui se base sur l'exemple `oven` de `sysrepo`.

## Arborescence

```
CMakeLists.txt
include/              en-tetes publics des modules
src/
  main.c              boucle FCGX_Accept_r multi-threads
  http.c              extraction FCGX_Request -> http_request / ecriture http_response
  restconf_path.c     analyse syntaxique des chemins RESTCONF (api-path RFC 8040 SS3.5.3.1)
  errors.c            modele d'erreur ietf-restconf:errors (RFC 8040 SS7-8)
  sysrepo_backend.c   connexion sysrepo, mapping datastores RFC 8527, lecture -> JSON
  restconf_handler.c  dispatcher HTTP -> sysrepo selon le type de ressource
etc/
  nginx-restconf.conf.example
yang/                 restconf yang schema
test/                 fichiers de test
fuzzing/              fichiers de fuzzing
```
