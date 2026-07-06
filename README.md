# 🚀 restconf-h2c-server

**Serveur RESTCONF haute performance, 100% asynchrone et mono-thread, basé sur HTTP/2 Cleartext (h2c) et Sysrepo.**

Ce projet implémente un backend RESTCONF conforme aux **RFC 8040** et **RFC 8527** (NMDA), ainsi que le support des notifications dynamiques via la **RFC 8650**. Conçu pour les environnements réseau modernes, il délègue la terminaison TLS à un Reverse Proxy et s'appuie sur le keyring du noyau Linux pour une gestion sécurisée et sans thread des identités JWT.

---

## ✨ Caractéristiques Principales

- 🌐 **Transport h2c (HTTP/2 Cleartext)** : Le backend ne gère pas le TLS. Il communique en HTTP/2 pur avec un Reverse Proxy (Nginx, HAProxy, Envoy) qui assure la terminaison HTTPS.
- 🧵 **Architecture 100% Mono-Thread & Non-Bloquante** : Zéro thread applicatif. Toute la magie opère dans une boucle `libevent` unique. Les descripteurs de fichiers (FD) de `sysrepo` sont intégrés directement dans `libevent` pour un traitement asynchrone des données et des notifications.
- 🔐 **Authentification JWT via Kernel Keyring** : La vérification cryptographique des JWT (signature RSA/EC générique via `EVP_DigestVerify`) utilise une clé publique stockée dans le **Linux Kernel Keyring**, lue via des `syscall()` bruts (`request_key`/`keyctl`, voir `CLAUDE.md` — **pas de dépendance à `libkeyutils`**), évitant ainsi l'exposition des clés en espace utilisateur.
- 🧩 **Plugin Sysrepo Intégré** : La logique métier (RPCs, données opérationnelles, monitoring) est gérée par un plugin `sysrepo` chargé dynamiquement dans le même espace d'adressage et la même boucle d'événements, éliminant les coûts d'IPC.
- 📡 **Notifications SSE sur HTTP/2** : Squelette de flux Server-Sent Events (SSE) sur streams HTTP/2 persistants (RFC 8650 / `ietf-restconf-subscribed-notifications`) — **en cours d'implémentation**, voir `ROADMAP.md` Phase 6.
- 🏛️ **Support NMDA (RFC 8527)** : Routage d'URI `/restconf/ds/<datastore>` **en cours d'implémentation** ; la sélection réelle du datastore sysrepo ainsi que `with-origin`/`with-defaults` ne sont pas encore fonctionnels — voir `ROADMAP.md` Phase 5.

---

## 🏗️ Architecture

```text
  [ Client RESTCONF ]
         │ (HTTPS / TLS 1.3)
         ▼
  [ Reverse Proxy (Nginx/HAProxy) ] ──(Extrait le JWT, valide le certificat)
         │ (h2c + Header: X-User-JWT)
         ▼
  ┌──────────────────────────────────────────────┐
  │  Backend C (restconf-h2c-server)             │
  │  ┌─────────────────────────────────────────┐ │
  │  │  libevent (Boucle d'événements unique)  │ │
  │  │  ├── nghttp2 (Moteur HTTP/2 h2c)        │ │
  │  │  ├── JWT Validator (OpenSSL + Keyring)  │ │
  │  │  └── Sysrepo FD (Lecture/Écriture SHM)  │ │
  │  └─────────────────────────────────────────┘ │
  │                      │                       │
  │  ┌───────────────────▼─────────────────────┐ │
  │  │  Plugin Sysrepo (Chargé in-process)     │ │
  │  │  ├── ietf-restconf-monitoring           │ │
  │  │  └── ietf-restconf-subscribed-notif...  │ │
  │  └─────────────────────────────────────────┘ │
  └──────────────────────────────────────────────┘
         │ (Shared Memory / IPC)
         ▼
  [ Sysrepo / libyang Datastores ]
```

---

## 📜 RFCs et Modules YANG Supportés

| RFC / Module | Description |
| :--- | :--- |
| **RFC 8040** | Protocole RESTCONF de base (CRUD, RPC, Actions, Query Params). |
| **RFC 8527** | Extensions NMDA (Nouveaux datastores, `with-origin`). |
| **RFC 8650** | Notifications dynamiques (SSE via HTTP/2). |
| `ietf-restconf` | Structures conceptuelles (`yang-errors`, `yang-api`). |
| `ietf-restconf-monitoring` | Exposition des capacités et flux SSE (`restconf-state`). |
| `ietf-restconf-subscribed-notifications` | Augmentation pour l'URI des flux RESTCONF. |

---

## 📦 Prérequis et Dépendances

Assurez-vous que les bibliothèques suivantes sont installées sur votre système :

- **libevent** (`libevent-dev`) : Boucle d'événements et multiplexage I/O.
- **nghttp2** (`libnghttp2-dev`) : Protocole HTTP/2.
- **sysrepo** & **libyang** : Moteur de données YANG et datastores.
- **OpenSSL** (`libssl-dev`) : Parsing et vérification cryptographique des JWT.
- **CMake** & **GCC/Clang** : Outils de compilation.

> ℹ️ Le Kernel Keyring est interrogé via les `syscall()` `request_key`/`keyctl`
> directement (voir `CLAUDE.md`, règle n°5) : **aucune dépendance à
> `libkeyutils` n'est nécessaire ni utilisée** dans le code. Le paquet
> `keyutils` (outil CLI `keyctl`) reste utile en exploitation pour injecter
> la clé (voir section Sécurité ci-dessous), mais n'est pas une dépendance
> de compilation.

```bash
# Exemple sur Ubuntu/Debian
sudo apt-get install cmake build-essential libevent-dev libnghttp2-dev \
libsysrepo-dev libyang-dev libssl-dev
```

---

## 🛠️ Compilation et Installation

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
# Génère uniquement : ./restconf-server
```

```bash
mkdir build-ext && cd build-ext
cmake -DBUILD_EXTERNAL_PLUGIN=ON ..
make -j$(nproc)
sudo make install
# Génère deux exécutables : ./restconf-server et ./restconf-plugin
```

### 🏗️ Architecture Dual-Mode (CMake Option)

#### Option CMake : `BUILD_EXTERNAL_PLUGIN`
*   **`OFF` (Défaut - Mode Interne)** : Le code du plugin est compilé en bibliothèque statique/interne et lié directement à l'exécutable principal. Partage le même espace mémoire et la même boucle `libevent`.
*   **`ON` (Mode Externe - Séparation des Privilèges)** : Le plugin est compilé comme un exécutable distinct. La communication se fait via IPC (UDS).

#### Mode Externe : Flux de Communication (UDS + libevent)
Puisqu'il est interdit d'utiliser des threads, l'IPC doit être non-bloquant. Le flux **cible** est le suivant :
1. **Le Plugin (Privilégié)** crée un socket Unix (`AF_UNIX`) et l'ajoute à sa propre boucle `libevent` via `evconnlistener`.
2. **Le Gateway HTTP (Non-privilégié)** se connecte à ce UDS et l'ajoute à sa boucle `libevent` via `bufferevent_socket_new`.
3. **Flux JWT / NACM** : Le Gateway reçoit le JWT, l'envoie au Plugin via le UDS. Le Plugin lit le Kernel Keyring, valide le JWT, et renvoie le `username` au Gateway. Le Gateway applique `sr_session_set_user()` (si les ACLs sysrepo le permettent) ou délègue la requête sysrepo au Plugin.
4. **Flux Notifications (SSE)** : Le Plugin (qui est abonné à `sysrepo`) reçoit un événement. Il sérialise la notification et l'écrit sur le UDS. Le `libevent` du Gateway déclenche un callback `EV_READ` sur le UDS, lit le payload, et l'injecte dans le stream HTTP/2 via `nghttp2_submit_data`.

> ⚠️ **État actuel** : le framing IPC (en-tête `ipc_msg_header_t`, magic
> `0x52434E46`) est implémenté dans `ipc/uds_common.c`, et la connexion
> UDS est établie des deux côtés. En revanche, la sérialisation/lecture
> des messages (`plugin_handle_get/edit/rpc`, `gateway_read_cb`,
> `uds_read_cb`) est encore à l'état de squelette (`TODO`), et le démon
> `restconf-plugin` ne se connecte pas encore à `sysrepo`. **Le mode
> externe n'est donc pas fonctionnel de bout en bout aujourd'hui** — voir
> `ROADMAP.md` Phase 3 pour le détail des tâches restantes.

---

## 🔐 Configuration de la Sécurité (Kernel Keyring)

Le backend ne stocke aucune clé en dur. La clé publique utilisée pour vérifier les JWT doit être injectée dans le keyring du noyau Linux avant le démarrage du service.

```bash
# 1. Ajouter la clé publique au keyring utilisateur ou système
# (Exemple avec une clé RSA au format PEM convertie en DER)
openssl rsa -pubin -in public_key.pem -outform DER -out public_key.der
keyctl add user restconf_jwt_pubkey "$(cat public_key.der)" @u

# 2. Notez le Key ID retourné (ex: 12345678)
# 3. Configurez le backend pour utiliser ce Key ID (via variable d'env ou config)
export RESTCONF_JWT_KEY_ID=12345678
```

---

## ⚙️ Configuration du Reverse Proxy (Nginx)

Le Reverse Proxy doit accepter l'HTTPS, extraire l'identité, et forwarder vers le backend en **h2c**.

```nginx
server {
    listen 443 ssl http2;
    server_name restconf.example.com;

    ssl_certificate     /etc/ssl/certs/server.crt;
    ssl_certificate_key /etc/ssl/private/server.key;
    
    # ... configuration de validation du certificat client ou JWT ...

    location /restconf {
        # Nginx >= 1.25.1 sait parler HTTP/2 cleartext (h2c) en amont
        # nativement via le mot-clé "http2" sur l'upstream. Sur des
        # versions plus anciennes, préférez HAProxy ou Envoy, qui gèrent
        # le h2c vers l'amont de longue date. Le module gRPC de Nginx
        # (`grpc_pass`) NE CONVIENT PAS ici : il impose un framing gRPC
        # (protobuf) et non du RESTCONF JSON/XML classique.
        proxy_pass http://127.0.0.1:8080;
        proxy_http_version 1.1;

        # Injection du JWT ou de l'utilisateur extrait dans un header
        proxy_set_header Authorization $http_authorization;
    }
}
```

---

## 🚀 Démarrage

```bash
# Lancer le démon sysrepo si ce n'est pas déjà fait
sysrepod &

# Lancer le serveur RESTCONF
./restconf-h2c-server --bind 127.0.0.1:8080 --keyring-id 12345678
```

---

## 📡 Exemples d'Utilisation

### 1. Découverte de la racine (Root Discovery)
```http
GET /.well-known/host-meta HTTP/2
Host: restconf.example.com
```

### 2. Lecture d'un datastore (NMDA - Operational)
```http
GET /restconf/ds/ietf-datastores:operational/ietf-interfaces:interfaces?with-origin HTTP/2
Host: restconf.example.com
Authorization: Bearer <jwt_token>
Accept: application/yang-data+json
```

### 3. Souscription aux notifications (RFC 8650)
```http
POST /restconf/operations/ietf-subscribed-notifications:establish-subscription HTTP/2
Host: restconf.example.com
Content-Type: application/yang-data+json

{
  "ietf-subscribed-notifications:input": {
    "stream": "ietf-yang-push:yang-push",
    "xpath-filter": "/ietf-interfaces:interfaces/interface/statistics"
  }
}
```
*Le plugin sysrepo retourne l'URI du flux SSE (ex: `/streams/NETCONF`) que le client consomme via un `GET` avec `Accept: text/event-stream` sur un stream HTTP/2 persistant.*

---

## 🧪 Tests et Conformité

> ⚠️ **État actuel** : `test/basic.sh` contient aujourd'hui quelques appels
> `nghttp` manuels (Root Discovery, API Resource JSON/XML) sans assertions
> automatisées, et `CMakeLists.txt` n'active pas encore CTest
> (`enable_testing()`/`add_test()` absents). La commande `ctest` ci-dessous
> est donc **l'objectif visé**, pas l'état actuel — voir `ROADMAP.md`
> Phase 7 (item 7.4).

Objectif de la suite de tests, une fois l'intégration CTest faite :
- La conformité stricte aux RFC 8040 et 8527.
- Le comportement non-bloquant sous charge (stress-test sur la boucle `libevent`).
- La validation des JWT et l'application du NACM (Network Configuration Access Control Model).

```bash
cd build
ctest --output-on-failure   # cible, pas encore câblé
```

---

```
restconf/
├── CMakeLists.txt
├── CLAUDE.md
├── include/
│   ├── h2c_server.h
│   ├── router.h
│   ├── jwt_validator.h
│   ├── plugin_api.h       # Interface d'abstraction (Appels directs ou IPC)
│   └── ipc/
│       └── uds_common.h   # Protocole de sérialisation pour les UDS
├── src/
│   ├── main.c
│   ├── h2c_server.c
│   ├── router.c
│   ├── jwt_validator.c
│   ├── sse_stream.c
│   ├── ipc/
│   │   ├── uds_common.c
│   │   ├── uds_gateway.c  # Compilé uniquement si BUILD_EXTERNAL_PLUGIN=ON
│   │   └── uds_plugin.c   # Compilé uniquement si BUILD_EXTERNAL_PLUGIN=ON
│   └── plugin/
│       ├── plugin_main.c  # Point d'entrée (main) du démon externe
│       ├── sysrepo_plugin.c
```

---

## 🤝 Contribuer

Les contributions sont les bienvenues ! Veuillez soumettre une *Pull Request* ou ouvrir une *Issue* pour discuter des modifications majeures. Assurez-vous de respecter la contrainte stricte **mono-thread / non-bloquant** lors de l'ajout de nouvelles fonctionnalités.