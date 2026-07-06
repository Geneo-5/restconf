# 🚀 restconf-h2c-server

**Serveur RESTCONF haute performance, 100% asynchrone et mono-thread, basé sur HTTP/2 Cleartext (h2c) et Sysrepo.**

Ce projet implémente un backend RESTCONF conforme aux **RFC 8040** et **RFC 8527** (NMDA), ainsi que le support des notifications dynamiques via la **RFC 8650**. Conçu pour les environnements réseau modernes, il délègue la terminaison TLS à un Reverse Proxy et s'appuie sur le keyring du noyau Linux pour une gestion sécurisée et sans thread des identités JWT.

---

## ✨ Caractéristiques Principales

- 🌐 **Transport h2c (HTTP/2 Cleartext)** : Le backend ne gère pas le TLS. Il communique en HTTP/2 pur avec un Reverse Proxy (Nginx, HAProxy, Envoy) qui assure la terminaison HTTPS.
- 🧵 **Architecture 100% Mono-Thread & Non-Bloquante** : Zéro thread applicatif. Toute la magie opère dans une boucle `libevent` unique. Les descripteurs de fichiers (FD) de `sysrepo` sont intégrés directement dans `libevent` pour un traitement asynchrone des données et des notifications.
- 🔐 **Authentification JWT via Kernel Keyring** : La vérification cryptographique des JWT (RS256/ES256) utilise les clés publiques stockées de manière sécurisée dans le **Linux Kernel Keyring** (`libkeyutils`), évitant ainsi l'exposition des clés en espace utilisateur.
- 🧩 **Plugin Sysrepo Intégré** : La logique métier (RPCs, données opérationnelles, monitoring) est gérée par un plugin `sysrepo` chargé dynamiquement dans le même espace d'adressage et la même boucle d'événements, éliminant les coûts d'IPC.
- 📡 **Notifications SSE sur HTTP/2** : Support natif des flux Server-Sent Events (SSE) via les streams HTTP/2 persistants (RFC 8650 / `ietf-restconf-subscribed-notifications`).
- 🏛️ **Support NMDA (RFC 8527)** : Accès direct aux datastores `operational`, `running`, `intended` avec support des paramètres `with-origin` et `with-defaults`.

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
- **libkeyutils** : Interaction avec le keyring du noyau Linux.
- **OpenSSL** (`libssl-dev`) : Parsing et vérification cryptographique des JWT.
- **CMake** & **GCC/Clang** : Outils de compilation.

```bash
# Exemple sur Ubuntu/Debian
sudo apt-get install cmake build-essential libevent-dev libnghttp2-dev \
libsysrepo-dev libyang-dev libkeyutils-dev libssl-dev
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
Puisqu'il est interdit d'utiliser des threads, l'IPC doit être non-bloquant.
1. **Le Plugin (Privilégié)** crée un socket Unix (`AF_UNIX`) et l'ajoute à sa propre boucle `libevent` via `evconnlistener`.
2. **Le Gateway HTTP (Non-privilégié)** se connecte à ce UDS et l'ajoute à sa boucle `libevent` via `bufferevent_socket_new`.
3. **Flux JWT / NACM** : Le Gateway reçoit le JWT, l'envoie au Plugin via le UDS. Le Plugin lit le Kernel Keyring, valide le JWT, et renvoie le `username` au Gateway. Le Gateway applique `sr_session_set_user()` (si les ACLs sysrepo le permettent) ou délègue la requête sysrepo au Plugin.
4. **Flux Notifications (SSE)** : Le Plugin (qui est abonné à `sysrepo`) reçoit un événement. Il sérialise la notification et l'écrit sur le UDS. Le `libevent` du Gateway déclenche un callback `EV_READ` sur le UDS, lit le payload, et l'injecte dans le stream HTTP/2 via `nghttp2_submit_data`.

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
        # Forward vers le backend en HTTP/2 Cleartext (h2c)
        grpc_pass grpc://127.0.0.1:8080; 
        
        # Injection du JWT ou de l'utilisateur extrait dans un header
        grpc_set_header X-User-JWT $http_authorization; 
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

Le projet inclut une suite de tests utilisant `sysrepo` et des clients RESTCONF de référence pour valider :
- La conformité stricte aux RFC 8040 et 8527.
- Le comportement non-bloquant sous charge (stress-test sur la boucle `libevent`).
- La validation des JWT et l'application du NACM (Network Configuration Access Control Model).

```bash
cd build
ctest --output-on-failure
```

---

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

---

## 🤝 Contribuer

Les contributions sont les bienvenues ! Veuillez soumettre une *Pull Request* ou ouvrir une *Issue* pour discuter des modifications majeures. Assurez-vous de respecter la contrainte stricte **mono-thread / non-bloquant** lors de l'ajout de nouvelles fonctionnalités.