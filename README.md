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
- **json-c** (`libjson-c-dev`) : Parsing JSON robuste (utilisé pour le payload JWT).
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
libsysrepo-dev libyang-dev libssl-dev libjson-c-dev
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

### Options CMake

| Option | Description | Défaut |
|--------|-------------|--------|
| `BUILD_EXTERNAL_PLUGIN` | Compiler le plugin comme exécutable séparé (UDS IPC) | `OFF` |
| `ALLOW_INSECURE_JWT` | Désactiver la vérification JWT (mode debug non sécurisé) | `OFF` |
| `LOG_LEVEL` | Niveau de log maximum à la compilation (0=TRACE à 5=FATAL) | `2` (INFO) |
| `PLUGIN_UDS_PATH` | Chemin de la socket Unix pour l'IPC serveur ↔ plugin | `/var/run/restconf-plugin.sock` |

**Exemples :**

```bash
# Compilation production (logs ERROR/FATAL uniquement, plugin externe)
cmake -B build_prod -DLOG_LEVEL=4 -DBUILD_EXTERNAL_PLUGIN=ON ..
make -C build_prod -j$(nproc)

# Compilation développement (tous les logs, chemin IPC personnalisé)
cmake -B build_dev -DLOG_LEVEL=0 -DBUILD_EXTERNAL_PLUGIN=ON \
      -DPLUGIN_UDS_PATH=/tmp/restconf.sock ..
make -C build_dev -j$(nproc)
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

### Ligne de commande

#### `restconf-server`

```bash
Usage: restconf-server [options]
Options:
  -a <addr>   Adresse d'écoute TCP h2c (défaut: 127.0.0.1)
  -p <port>   Port d'écoute TCP h2c (défaut: 8080)
  -u <path>   Écouter sur socket Unix (h2c) au lieu de TCP
  -k <desc>   Nom du descripteur de clé JWT dans le keyring (défaut: restconf_jwt_pubkey)
  -d          Mode daemon (fork en arrière-plan, force syslog)
  -s          Utiliser syslog au lieu de stdout
  -v <level>  Niveau de log à l'exécution (0=TRACE, 1=DEBUG, 2=INFO, 3=WARN, 4=ERROR, 5=FATAL)
  -h          Afficher l'aide
```

> ℹ️ En mode externe (`BUILD_EXTERNAL_PLUGIN=ON`), l'aide affiche
> également le chemin de la socket IPC vers le plugin, fixé à la
> compilation via `PLUGIN_UDS_PATH`.

**Exemples :**

```bash
# Mode TCP (par défaut) — le reverse proxy forward le h2c ici
./restconf-server -a 0.0.0.0 -p 8443

# Mode socket Unix h2c — le reverse proxy se connecte à cette socket
./restconf-server -u /var/run/restconf-h2c.sock

# Mode daemon avec syslog et clé JWT personnalisée
./restconf-server -u /var/run/restconf-h2c.sock -k my_custom_key -d

# Mode développement avec tous les logs
./restconf-server -v 0
```

#### `restconf-plugin` (mode externe uniquement)

```bash
Usage: restconf-plugin [options]
Options:
  -d          Mode daemon (fork en arrière-plan, force syslog)
  -s          Utiliser syslog au lieu de stdout
  -v <level>  Niveau de log à l'exécution (0=TRACE, 1=DEBUG, 2=INFO, 3=WARN, 4=ERROR, 5=FATAL)
  -h          Afficher l'aide
```

> ℹ️ Le chemin de la socket IPC est **fixé à la compilation** via
> l'option CMake `PLUGIN_UDS_PATH` (défaut :
> `/var/run/restconf-plugin.sock`). Il n'y a pas d'option `-u` pour
> le surcharger à l'exécution : le serveur et le plugin doivent
> toujours utiliser le même chemin, garanti par la compilation.

**Exemples :**

```bash
# Lancer le plugin externe (utilise PLUGIN_UDS_PATH compilé)
./restconf-plugin

# Mode daemon avec syslog
./restconf-plugin -d -s
```

### Démarrage complet

```bash
# 1. Lancer le démon sysrepo si ce n'est pas déjà fait
sysrepod &

# 2a. Mode interne (monolithique)
./restconf-server -a 127.0.0.1 -p 8080

# 2b. Mode externe (séparation des privilèges)
./restconf-plugin &   # écoute sur PLUGIN_UDS_PATH
./restconf-server -a 127.0.0.1 -p 8080   # se connecte à PLUGIN_UDS_PATH
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

La suite de tests est écrite en **Python 3** avec **pytest**. Elle démarre automatiquement le serveur RESTCONF, exécute les tests, puis l'arrête.

### Prérequis

```bash
pip install -r test/requirements.txt
```

### Exécution

```bash
# Compiler le serveur d'abord
cmake -B build -DLOG_LEVEL=0 . && make -C build -j$(nproc)

# Lancer tous les tests
pytest test/ -v

# Lancer un test spécifique
pytest test/test_basic.py::TestRootDiscovery::test_host_meta -v

# Lancer avec un serveur personnalisé
RESTCONF_SERVER_BIN=./build_prod/restconf-server \
RESTCONF_PORT=9090 \
pytest test/ -v
```

### Structure des tests

| Fichier | Description |
|---------|-------------|
| `test/conftest.py` | Configuration pytest, fixtures (serveur, sessions HTTP) |
| `test/test_basic.py` | Root Discovery (RFC 8040 §3.1), API Resource (§3.2), méthodes HTTP |

### Variables d'environnement

| Variable | Description | Défaut |
|----------|-------------|--------|
| `RESTCONF_SERVER_BIN` | Chemin vers l'exécutable du serveur | `./build/restconf-server` |
| `RESTCONF_BIND_ADDR` | Adresse IP d'écoute du serveur | `127.0.0.1` |
| `RESTCONF_PORT` | Port d'écoute du serveur | `8080` |

> ℹ️ L'intégration **CTest** (pour `ctest --output-on-failure`) est prévue
> dans `ROADMAP.md` Phase 7 (item 7.4).

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