# CLAUDE.md - Project Guidelines for RESTCONF h2c Backend

## 📖 Project Overview
This project is a high-performance, strictly single-threaded, and 100% asynchronous RESTCONF backend server written in C. It implements **RFC 8040** (RESTCONF), **RFC 8527** (NMDA extensions), and **RFC 8650** (Subscribed Notifications via `ietf-restconf-subscribed-notifications`). 

It operates exclusively over **HTTP/2 Cleartext (h2c)**, delegating TLS termination to a reverse proxy. Authentication is handled via JWT, with cryptographic verification relying on the **Linux Kernel Keyring**. The business logic is implemented as an **internal or external sysrepo plugin** (configurable via CMake for privilege separation).

## 🚨 CRITICAL CONSTRAINTS (THE "GOLDEN RULES")
**Violating these rules will break the architecture. AI agents MUST adhere to them strictly.**

1. **NO THREADS (`pthread`, `stdthread`, etc. are FORBIDDEN):** The entire application must run in a single thread driven by the `libevent` event loop. 
2. **NO BLOCKING CALLS:** Never block the `libevent` loop. All I/O, network operations, and sysrepo IPC must be asynchronous.
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

