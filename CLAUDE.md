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

## 📏 Code Style & Formatting (STRICT)
**All generated C code and scripts MUST strictly follow these formatting rules:**
- **Indentation:** You MUST use **TAB** characters for indentation. The tab width is visually set to **8**. **NEVER** use spaces for indentation.
- **Line Length:** The maximum line length is strictly **80 characters**. You MUST wrap and break lines to respect this limit. Do not exceed 80 columns.
- **Align:** align start with **TAB** until indent length and finish with **SPACE**
```c
#define FIX_SAMPLE_PACKED_SIZE_MAX (DPACK_UINT8_SIZE_MAX + \
                                    DPACK_UINT16_SIZE_MAX + \
                                    DPACK_UINT32_SIZE_MAX)

extern int
map_sample_pack(struct dpack_encoder    * encoder,
                const struct map_sample * sample);
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
- `sysrepo` file descriptors (obtained via `sr_get_event_fd()`) are registered in `libevent` using `event_new(..., EV_READ)`.
- When `sysrepo` has data/notifications, the FD triggers a `libevent` callback, which calls `sr_subscription_process_events()` **inside the main thread**.

## 💻 Coding Guidelines & Patterns

### 1. Sysrepo Integration (Async & FD Mapping)
Always prefer asynchronous sysrepo APIs to prevent blocking the event loop during IPC.
```c
/* GOOD: Asynchronous data retrieval */
sr_get_data_async(session, xpath, 0, 0, 0, my_data_cb, req_ctx);

/* BAD: Synchronous blocking call in the main loop */
sr_get_data(session, xpath, 0, 0, 0, &data); 

## Syrepo sample code

```c
/**
 * @file oven.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief oven example plugin
 *
 * @copyright
 * Copyright (c) 2019 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#define _QNX_SOURCE /* sleep() */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libyang/libyang.h>
#include <sysrepo.h>

/* no synchronization is used in this example even though most of these
 * variables are shared between 2 threads, but the chances of encountering
 * problems is low enough to ignore them in this case */

/* session of our plugin, can be used until cleanup is called */
sr_session_ctx_t *sess;
/* structure holding all the subscriptions */
sr_subscription_ctx_t *subscription;
/* thread ID of the oven (thread) */
volatile pthread_t oven_tid;
/* oven state value determining whether the food is inside the oven or not */
volatile int food_inside;
/* oven state value determining whether the food is waiting for the oven to be ready */
volatile int insert_food_on_ready;
/* oven state value determining the current temperature of the oven */
volatile unsigned int oven_temperature;
/* oven config value stored locally just so that it is not needed to ask sysrepo for it all the time */
volatile unsigned int config_temperature;

static void *
oven_thread(void *arg)
{
    int rc;
    unsigned int desired_temperature;

    (void)arg;

    while (oven_tid) {
        sleep(1);
        if (oven_temperature < config_temperature) {
            /* oven is heating up 50 degrees per second until the set temperature */
            if (oven_temperature + 50 < config_temperature) {
                oven_temperature += 50;
            } else {
                oven_temperature = config_temperature;
                /* oven reached the desired temperature, create a notification */
                rc = sr_notif_send(sess, "/oven:oven-ready", NULL, 0, 0, 0);
                if (rc != SR_ERR_OK) {
                    SRPLG_LOG_ERR("oven", "Oven-ready notification generation failed: %s.", sr_strerror(rc));
                }
            }
        } else if (oven_temperature > config_temperature) {
            /* oven is cooling down but it will never be colder than the room temperature */
            desired_temperature = (config_temperature < 25 ? 25 : config_temperature);
            if (oven_temperature - 20 > desired_temperature) {
                oven_temperature -= 20;
            } else {
                oven_temperature = desired_temperature;
            }
        }

        if (insert_food_on_ready && (oven_temperature >= config_temperature)) {
            /* food is inserted once the oven is ready */
            insert_food_on_ready = 0;
            food_inside = 1;
            SRPLG_LOG_DBG("oven", "Food put into the oven.");
        }
    }

    return NULL;
}

static int
oven_config_change_cb(sr_session_ctx_t *session, uint32_t sub_id, const char *module_name, const char *xpath,
        sr_event_t event, uint32_t request_id, void *private_data)
{
    int rc;
    sr_val_t *val;
    pthread_t tid;

    (void)sub_id;
    (void)module_name;
    (void)xpath;
    (void)event;
    (void)request_id;
    (void)private_data;

    /* get the value from sysrepo, we do not care if the value did not change in our case */
    rc = sr_get_item(session, "/oven:oven/temperature", 0, &val);
    if (rc != SR_ERR_OK) {
        goto sr_error;
    }

    config_temperature = val->data.uint8_val;
    sr_free_val(val);

    rc = sr_get_item(session, "/oven:oven/turned-on", 0, &val);
    if (rc != SR_ERR_OK) {
        goto sr_error;
    }

    if (val->data.bool_val && (oven_tid == 0)) {
        /* the oven should be turned on and is not (create the oven thread) */
        rc = pthread_create((pthread_t *)&oven_tid, NULL, oven_thread, NULL);
        if (rc != 0) {
            goto sys_error;
        }
    } else if (!val->data.bool_val && (oven_tid != 0)) {
        /* the oven should be turned off but is on (stop the oven thread) */
        tid = oven_tid;
        oven_tid = 0;
        rc = pthread_join(tid, NULL);
        if (rc != 0) {
            goto sys_error;
        }

        /* we pretend the oven cooled down immediately after being turned off */
        oven_temperature = 25;
    }
    sr_free_val(val);

    return SR_ERR_OK;

sr_error:
    SRPLG_LOG_ERR("oven", "Oven config change callback failed: %s.", sr_strerror(rc));
    return rc;

sys_error:
    sr_free_val(val);
    SRPLG_LOG_ERR("oven", "Oven config change callback failed: %s.", strerror(rc));
    return SR_ERR_OPERATION_FAILED;
}

static int
oven_state_cb(sr_session_ctx_t *session, uint32_t sub_id, const char *module_name, const char *path,
        const char *request_xpath, uint32_t request_id, struct lyd_node **parent, void *private_data)
{
    const struct ly_ctx *ly_ctx;
    char str[32];

    (void)session;
    (void)sub_id;
    (void)module_name;
    (void)path;
    (void)request_xpath;
    (void)request_id;
    (void)private_data;

    ly_ctx = sr_acquire_context(sr_session_get_connection(sess));
    sprintf(str, "%u", oven_temperature);
    lyd_new_path(NULL, ly_ctx, "/oven:oven-state/temperature", str, 0, parent);
    lyd_new_path(*parent, NULL, "/oven:oven-state/food-inside", food_inside ? "true" : "false", 0, NULL);
    sr_release_context(sr_session_get_connection(sess));

    return SR_ERR_OK;
}

static int
oven_insert_food_cb(sr_session_ctx_t *session, uint32_t sub_id, const char *path, const sr_val_t *input,
        const size_t input_cnt, sr_event_t event, uint32_t request_id, sr_val_t **output, size_t *output_cnt,
        void *private_data)
{
    (void)session;
    (void)sub_id;
    (void)path;
    (void)input;
    (void)input_cnt;
    (void)event;
    (void)request_id;
    (void)output;
    (void)output_cnt;
    (void)private_data;

    if (food_inside) {
        SRPLG_LOG_ERR("oven", "Food already in the oven.");
        return SR_ERR_OPERATION_FAILED;
    }

    if (strcmp(input[0].data.enum_val, "on-oven-ready") == 0) {
        if (insert_food_on_ready) {
            SRPLG_LOG_ERR("oven", "Food already waiting for the oven to be ready.");
            return SR_ERR_OPERATION_FAILED;
        }
        insert_food_on_ready = 1;
        return SR_ERR_OK;
    }

    insert_food_on_ready = 0;
    food_inside = 1;
    SRPLG_LOG_DBG("oven", "Food put into the oven.");
    return SR_ERR_OK;
}

static int
oven_remove_food_cb(sr_session_ctx_t *session, uint32_t sub_id, const char *path, const sr_val_t *input,
        const size_t input_cnt, sr_event_t event, uint32_t request_id, sr_val_t **output, size_t *output_cnt,
        void *private_data)
{
    (void)session;
    (void)sub_id;
    (void)path;
    (void)input;
    (void)input_cnt;
    (void)event;
    (void)request_id;
    (void)output;
    (void)output_cnt;
    (void)private_data;

    if (!food_inside) {
        SRPLG_LOG_ERR("oven", "Food not in the oven.");
        return SR_ERR_OPERATION_FAILED;
    }

    food_inside = 0;
    SRPLG_LOG_DBG("oven", "Food taken out of the oven.");
    return SR_ERR_OK;
}

int
sr_plugin_init_cb(sr_session_ctx_t *session, void **private_data)
{
    int rc;

    (void)private_data;

    /* remember the session of our plugin */
    sess = session;

    /* initialize the oven state */
    food_inside = 0;
    insert_food_on_ready = 0;
    /* room temperature */
    oven_temperature = 25;

    /* subscribe for oven module changes - also causes startup oven data to be copied into running and enabling the module */
    rc = sr_module_change_subscribe(session, "oven", NULL, oven_config_change_cb, NULL, 0,
            SR_SUBSCR_ENABLED | SR_SUBSCR_DONE_ONLY, &subscription);
    if (rc != SR_ERR_OK) {
        goto error;
    }

    /* subscribe as state data provider for the oven state data */
    rc = sr_oper_get_subscribe(session, "oven", "/oven:oven-state", oven_state_cb, NULL, 0, &subscription);
    if (rc != SR_ERR_OK) {
        goto error;
    }

    /* subscribe for insert-food RPC calls */
    rc = sr_rpc_subscribe(session, "/oven:insert-food", oven_insert_food_cb, NULL, 0, 0, &subscription);
    if (rc != SR_ERR_OK) {
        goto error;
    }

    /* subscribe for remove-food RPC calls */
    rc = sr_rpc_subscribe(session, "/oven:remove-food", oven_remove_food_cb, NULL, 0, 0, &subscription);
    if (rc != SR_ERR_OK) {
        goto error;
    }

    /* sysrepo/plugins.h provides an interface for logging */
    SRPLG_LOG_DBG("oven", "Oven plugin initialized successfully.");
    return SR_ERR_OK;

error:
    SRPLG_LOG_ERR("oven", "Oven plugin initialization failed: %s.", sr_strerror(rc));
    sr_unsubscribe(subscription);
    return rc;
}

void
sr_plugin_cleanup_cb(sr_session_ctx_t *session, void *private_data)
{
    (void)session;
    (void)private_data;

    /* nothing to cleanup except freeing the subscriptions */
    sr_unsubscribe(subscription);
    SRPLG_LOG_DBG("oven", "Oven plugin cleanup finished.");
}

```
