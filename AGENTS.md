# CLAUDE.md - Project Guidelines for RESTCONF h2c Backend

## 📖 Project Overview
This project is a high-performance, strictly single-threaded, and 100% asynchronous RESTCONF backend server written in C. It implements **RFC 8040** (RESTCONF), **RFC 8527** (NMDA extensions), and **RFC 8650** (Subscribed Notifications via `ietf-restconf-subscribed-notifications`). 

It operates exclusively over **HTTP/2 Cleartext (h2c)**, delegating TLS termination to a reverse proxy. Authentication is handled via JWT, with cryptographic verification relying on the **Linux Kernel Keyring**. The business logic is implemented as an **internal or external sysrepo plugin** (configurable via CMake for privilege separation).

## 🚨 CRITICAL CONSTRAINTS (THE "GOLDEN RULES")
**Violating these rules will break the architecture. AI agents MUST adhere to them strictly.**

1. **NO THREADS, WITH ONE SINGLE DOCUMENTED EXCEPTION:** The HTTP/2 gateway (routing, JWT, NACM, codec, h2c) must run in a single thread driven by the `libevent` event loop. `pthread`/`stdthread` are FORBIDDEN there.
   **Exception (ROADMAP.md item 3.12, decided 2026-07-12):** `libsysrepo` exposes no asynchronous variant of `sr_get_data()`, `sr_apply_changes()`, `sr_rpc_send_tree()` or `sr_subscription_process_events()` — these are blocking calls by design. A single confined worker `pthread` (`src/plugin/sysrepo_worker.c`) is therefore tolerated as the **sole owner** of the sysrepo connection/sessions, strictly isolated behind a message-passing boundary (job queue in, completion queue out, both notified via pipes registered in `libevent`). No mutex is ever shared with HTTP/business-logic state; no sysrepo/libyang call other than `sr_acquire_context()`/`sr_release_context()` (documented by sysrepo as connection-level thread-safe) may happen outside that worker thread. Do not add any other thread, and do not let sysrepo calls creep back onto the `libevent` thread.
2. **NO BLOCKING CALLS ON THE LIBEVENT THREAD:** Never block the `libevent` loop. All I/O, network operations, and sysrepo access must be asynchronous from the gateway's point of view — sysrepo's own blocking nature is confined to the worker thread described in rule #1's exception, never called directly from `libevent` callbacks.",
3. **NO TLS/HTTPS:** The backend speaks **h2c only**. Do not implement OpenSSL TLS server logic. Trust the reverse proxy for TLS.
4. **DUAL-MODE PLUGIN:** The sysrepo plugin must be compilable as an internal (in-process) or external (out-of-process via UDS) daemon using the CMake option `BUILD_EXTERNAL_PLUGIN`.
5. **NO LIBKEYUTILS:** Do not use the `libkeyutils` library. You MUST use raw Linux syscalls (`syscall(__NR_request_key, ...)`, `syscall(__NR_keyctl, ...)`) to interact with the Kernel Keyring. Include `<linux/keyctl.h>` and `<sys/syscall.h>`.
6. **INSECURE JWT MODE:** The `ALLOW_INSECURE_JWT` CMake option disables cryptographic signature verification for debugging. Do not remove the `#ifdef ALLOW_INSECURE_JWT` guards in `jwt_validator.c`. This mode MUST NEVER be enabled in production builds.
7. **UPDATE RAODMAP.md**: The file RAODMAP.md must be update each time.

## 📏 Code Style & Formatting (STRICT)
**All generated C code and scripts MUST strictly follow these formatting rules:**
- **Indentation:** You MUST use **TAB** characters for indentation. The tab width is visually set to **8**. **NEVER** use spaces for indentation.
- **Line Length:** The maximum line length is strictly **80 characters**. You MUST wrap and break lines to respect this limit. Do not exceed 80 columns.
- **Align:** align start with **TAB** until indent length and finish with **SPACE**

- **No trailing whitespace:** No line may end with spaces or tabs. Strip them systematically (preferably in diffs).

- **Comments in English:** All comments in `.c` and `.h` files must be written in English.

- **Doxygen function descriptions:** Public function declarations (`.h`) and definitions (`.c`) must use the Doxygen `/** ... */` format. Each function must include:
  - `@brief`: concise one-line description
  - `@param[in]` / `@param[out]` / `@param[in, out]`: for each parameter
  - `@return`: return value (when applicable)
  - `@warning` or `@note`: additional notes

- **Sample code style:**
```h
/**
 * Find the closest upper power of 2 of a 32 bits word.
 *
 * @param[in] value word
 *
 * @return closest upper power of 2
 *
 * @warning
 * When compiled with the #CONFIG_ASSERT_API build option disabled and
 * @p value is zero, result is undefined. A zero @p value triggers an
 * assertion otherwise.
 */
extern unsigned int
stroll_pow2_up32(uint32_t value) __const __nothrow __leaf;
```

```c
unsigned int
stroll_pow2_up32(uint32_t value)
{
	/* Would overflow otherwise... */
	stroll_pow2_assert_api(value);

	if (value > (UINT32_C(1) << 31))
		return 32;

	return stroll_pow2_low32(value +
	                         (UINT32_C(1) << stroll_pow2_low32(value)) -
	                         1);
}
```

## BUILD and TEST
To run build and test, run :
```sh
./scripts/build_test.sh
```

## 🏗️ Architecture & Tech Stack

- **Event Loop:** `libevent` (Master of all I/O and timers).
- **HTTP/2 Engine:** `nghttp2` (Configured for h2c, Prior Knowledge or Upgrade).
- **Datastore:** `sysrepo` & `libyang` (YANG data modeling, NMDA datastores).
- **Security:** `libkeyutils` (Kernel Keyring) + `OpenSSL` (In-memory JWT crypto verification).
- **Language:** C11, CMake build system.

### Data Flow & Event Integration
- `libevent` listens on a TCP socket.
- `nghttp2` parses HTTP/2 frames.
- `libyang` sysrepo use libyang2
- `sysrepo` file descriptors (obtained via `sr_get_event_fd()`) are registered in `libevent` using `event_new(..., EV_READ)`.
- When `sysrepo` has data/notifications, the FD triggers a `libevent` callback, which calls `sr_subscription_process_events()` **inside the main thread**.

## 🧪 Testing Architecture

### CRITICAL: Separation of Concerns for Test Module

**`test/restconf-test.c`** is the **ONLY** file that must register callbacks on nodes from the `restconf-test.yang` module. This includes:
- RPC handlers via `sr_rpc_subscribe_tree()`
- Change subscription handlers via `sr_module_change_subscribe()`
- Operation data providers via `sr_oper_get_items_subscribe()`

**The RESTCONF server itself (`src/plugin/sysrepo_plugin.c` and `src/plugin/sysrepo_worker.c`) must NEVER register callbacks on `restconf-test.yang` nodes.**

**Rationale:**
- `restconf-test.yang` is a test-only module designed to validate the RESTCONF server's ability to route requests to external plugins
- The server acts as a transparent gateway: it receives RESTCONF requests and forwards them to sysrepo via `sr_rpc_send_tree()`, `sr_get_data()`, etc.
- sysrepo routes these calls to whichever plugin has registered the appropriate callbacks
- In production, real YANG modules would be handled by real external plugins (network device drivers, configuration managers, etc.)
- For testing, `test/restconf-test.c` simulates these external plugins

**Loading Mechanism:**
1. Build: `make test-install` compiles `test/restconf-test.c` into `restconf-test.so`
2. Installation: The `.so` is copied to `/usr/lib/sysrepo/plugins/`
3. Runtime: `sysrepo-plugind` (started by `entrypoint.sh`) automatically loads all `.so` files from that directory
4. Plugin initialization: `sr_plugin_init_cb()` in `test/restconf-test.c` registers the 6 RPC callbacks and any other handlers
5. Test execution: When the RESTCONF server receives a request for `/restconf/operations/restconf-test:*`, sysrepo routes it to the external plugin's callback

**What happens if the server registers on test nodes:**
- Duplicate callback registration → undefined behavior
- Violates separation of concerns (gateway vs. business logic)
- Makes it impossible to test the real plugin-loading scenario
- Breaks the architectural principle that the server is a pure RESTCONF gateway

