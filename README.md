# 🚀 restconf-h2c-server

**RESTCONF server, based on HTTP/2 Cleartext (h2c) and Sysrepo.**

`restconfd` is a RESTCONF backend ([RFC 8040](https://datatracker.ietf.org/doc/html/rfc8040))
written in C, built on top of `libevent` and `libnghttp2`. It speaks plain HTTP/2 (**h2c**, no
TLS) and is designed to be deployed **behind a reverse proxy** that terminates TLS/HTTPS on
the client side and forwards traffic to the backend in h2c.

The project is under active development — see [`ROADMAP.md`](ROADMAP.md) for the detailed
functional progress (covered RFCs, per-item status, planned conformance tests).

## Architecture

```
HTTPS/HTTP2 client
       │  TLS + ALPN "h2"
       ▼
   Reverse proxy (TLS termination)
       │  h2c "prior knowledge"
       ▼
   restconfd (h2c backend, this repo)
       │
       ▼
   sysrepo / libyang
```

- `restconfd` speaks **HTTP/2 in cleartext only**: it does not terminate TLS and does not
  handle HTTP/1.1. It must always run behind a reverse proxy that terminates TLS on the
  client side and forwards requests to the backend in h2c *prior knowledge*.
- Any reverse proxy able to do TLS termination and speak h2c to the backend can be used
  (HAProxy, nginx, Apache/`mod_proxy_http2`, Envoy, …). `docker/haproxy.cfg` is only **one
  example** configuration (used for CI, see below) — it is not a requirement to use HAProxy
  specifically.
- Data access goes through **sysrepo** (`libsysrepo` / `libyang`), either built into the
  binary (`SYSREPO_BUILTIN`), as a `sysrepo-plugind` plugin, or as a standalone daemon,
  depending on the `Config.in` configuration.

## Dependencies

The project depends on the following libraries:

| Dependency | Role |
|---|---|
| [sysrepo](https://github.com/sysrepo/sysrepo) / [libyang](https://github.com/CESNET/libyang) | Access to YANG data / datastores |
| [libevent](https://libevent.org/) | Event loop, sockets, timeouts |
| [libnghttp2](https://github.com/nghttp2/nghttp2) | HTTP/2 (h2c) implementation |
| [libjwt](https://github.com/benmcollins/libjwt) | JWT token validation (upcoming, see roadmap R2) |
| libcurl | Parsing of RESTCONF URIs (`:path`) |
| [ebuild](https://github.com/grgbr/ebuild), [stroll](https://github.com/grgbr/stroll), [utils](https://github.com/grgbr/utils) | Build system (Kconfig + Makefiles) and internal utility libraries |

The whole server (and any RESTCONF application code still to come: data handlers, NACM,
subscriptions, etc.) is written entirely in **C** — no Python on the server side. Python is
only used for the CI test suite (`pytest`), see below.

## Building

The build system is [eBuild](https://github.com/grgbr/ebuild) (Kconfig + Make). After
installing the dependencies listed above (`sysrepo`, `libyang`, `libevent`, `libnghttp2`,
`libjwt`, `libcurl`, plus `stroll`, `utils` and `ebuild` itself — see
[`docker/Dockerfile`](docker/Dockerfile) for a scripted reference of how to fetch and build
each of them):

```sh
# Interactive configuration (sysrepo mode, YANG modules path, HTTP/2 limits, etc.)
make menuconfig     # or: make defconfig

# Build
make -j"$(nproc)"

# Install (SBINDIR etc. defined by eBuild)
make install
```

Notable `Config.in` options:

- **Sysrepo mode**: `SYSREPO_BUILTIN` (built into the `restconfd` binary, default),
  `SYSREPO_PLUGIND` (plugin loaded by `sysrepo-plugind`), or `SYSREPO_DAEMON` (standalone
  `sysrepo-restconf` daemon).
- `RESTCONF_ASSERT`: enables internal assertions (disabled by default).
- `YANG_PATH`: path to YANG modules (default `/usr/share/yang/modules`).
- `H2C_MAX_REQUEST_BODY_SIZE`, `H2C_MAX_CONCURRENT_STREAMS`, `H2C_INITIAL_WINDOW_SIZE`,
  `H2C_CONNECTION_WINDOW_SIZE`: backend-side HTTP/2 limits.
- `H2C_RESTCONF_ROOT`: exposed `{+restconf}` path (default `/restconf`).

## Running `restconfd`

```
Usage: restconfd [options]
Options:
  -a <addr>   Bind address (default: 127.0.0.1)
  -p <port>   Port to listen on (default: 8080)
  -u <path>   Listen on a Unix socket (h2c) instead of TCP
  -d          Run as a daemon (background)
  -t <sec>    Idle read timeout per connection, 0=disabled (default: 300)
  -h          Show this help
```

`restconfd` never terminates TLS and never speaks HTTP/1.1 by itself: in any real deployment
it must sit behind a TLS-terminating reverse proxy speaking h2c prior knowledge to the
backend (over TCP or a Unix socket via `-u`).

## CI / running the test suite with Docker

[`docker/Dockerfile`](docker/Dockerfile) and the [`scripts/`](scripts) directory are **not** a
deployment or build recommendation — they exist solely to provide a reproducible environment
for **CI** in which the automated test suite (`pytest`) is run. They build every dependency
from source, compile `restconfd` with hardened flags and sanitizers
(`-fsanitize=address,undefined,leak,pointer-compare,pointer-subtract`), wire it up behind an
example reverse-proxy configuration (`docker/haproxy.cfg`, using HAProxy purely as one
possible example — any TLS-terminating, h2c-capable proxy would do), and run the tests inside
the container.

### Run the CI test suite

```sh
./scripts/build.sh
```

Options accepted by [`scripts/build.sh`](scripts/build.sh):

| Option | Effect |
|---|---|
| `--no-cache` | Rebuild the Docker image without using the Docker cache |
| `--tag=NAME` | Docker image tag (default: `restconfd:test-plugin`) |
| `--jwt-on` / `--jwt-off` | Enable/disable "insecure" JWT test mode (default: `--jwt-on`) |
| `--verbose[=N]` | Enable verbose pytest output (`-vv`) and set sysrepo log level `N` (0=TRACE … 5=EMERGENCY, default 2) |
| `--listen` / `--listen=PORT` | Run the container in interactive listening mode (ports 80/443 exposed) instead of running the test suite — useful for manual/exploratory testing against the CI image, not a production deployment mode |
| `-h`, `--help` | Show help |

Any extra argument is forwarded to `pytest` inside the container.

At container startup, [`docker/entrypoint.sh`](docker/entrypoint.sh):

- starts `sysrepo-plugind` in the background;
- starts the example reverse proxy (`haproxy`) in the background;
- starts `restconfd` on the Unix socket `/run/restconf.socket` (in `--listen` mode: in the
  foreground, blocking; otherwise in the background);
- runs the `pytest test/` suite (except in `--listen` mode).

### Quick manual check with curl

[`scripts/curl.sh`](scripts/curl.sh) shows a few basic requests against a running instance
(test `alg=none` JWT, `.well-known` discovery, `/restconf` root) and is meant to be used
against a container started with `./scripts/build.sh --listen`:

```sh
./scripts/build.sh --listen &
./scripts/curl.sh
```

## License

This project is distributed under the **LGPL-3.0-only** license (see
[`COPYING.txt`](COPYING.txt) and [`COPYING.LESSER`](COPYING.LESSER)).

## Project status

The detailed RFC-by-RFC, item-by-item status, along with the planned conformance tests, is
tracked in [`ROADMAP.md`](ROADMAP.md).
