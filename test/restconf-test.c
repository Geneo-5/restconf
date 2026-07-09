/**
 * resconf-test.c - Plugin Sysrepo pour le module restconf-test.yang
 * 
 * Plugin externe charge par sysrepo-plugind.
 * Il gere les RPCs du module restconf-test.yang.
 * 
 * Les donnees de configuration sont gerees directement par sysrepo.
 * Ce plugin implémente les callbacks RPC pour répondre aux requêtes RESTCONF.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sysrepo.h>

// Definir UNUSED si non defini
#ifndef UNUSED
#define UNUSED __attribute__((unused))
#endif

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define PLUGIN_NAME "restconf-test-plugin"
#define MODULE_NAME "restconf-test"

// ---------------------------------------------------------------------------
// Structure pour stocker le contexte du plugin
// ---------------------------------------------------------------------------
typedef struct test_plugin_ctx_s {
    sr_conn_ctx_t *connection;
    sr_session_ctx_t *session;
    sr_subscription_ctx_t *subscription;
} test_plugin_ctx_t;

// ---------------------------------------------------------------------------
// RPC Callback declarations
// ---------------------------------------------------------------------------
static int rpc_get_system_status_cb(
    sr_session_ctx_t *session UNUSED,
    uint32_t sub_id UNUSED,
    const char *path UNUSED,
    const sr_val_t *input UNUSED,
    const size_t input_cnt UNUSED,
    sr_event_t event UNUSED,
    uint32_t request_id UNUSED,
    sr_val_t **output,
    size_t *output_cnt,
    void *private_ctx UNUSED);

static int rpc_configure_device_cb(
    sr_session_ctx_t *session UNUSED,
    uint32_t sub_id UNUSED,
    const char *path UNUSED,
    const sr_val_t *input,
    const size_t input_cnt UNUSED,
    sr_event_t event UNUSED,
    uint32_t request_id UNUSED,
    sr_val_t **output,
    size_t *output_cnt,
    void *private_ctx UNUSED);

static int rpc_create_resource_cb(
    sr_session_ctx_t *session UNUSED,
    uint32_t sub_id UNUSED,
    const char *path UNUSED,
    const sr_val_t *input,
    const size_t input_cnt UNUSED,
    sr_event_t event UNUSED,
    uint32_t request_id UNUSED,
    sr_val_t **output,
    size_t *output_cnt,
    void *private_ctx UNUSED);

static int rpc_set_operation_mode_cb(
    sr_session_ctx_t *session UNUSED,
    uint32_t sub_id UNUSED,
    const char *path UNUSED,
    const sr_val_t *input,
    const size_t input_cnt UNUSED,
    sr_event_t event UNUSED,
    uint32_t request_id UNUSED,
    sr_val_t **output,
    size_t *output_cnt,
    void *private_ctx UNUSED);

static int rpc_process_data_cb(
    sr_session_ctx_t *session UNUSED,
    uint32_t sub_id UNUSED,
    const char *path UNUSED,
    const sr_val_t *input UNUSED,
    const size_t input_cnt UNUSED,
    sr_event_t event UNUSED,
    uint32_t request_id UNUSED,
    sr_val_t **output,
    size_t *output_cnt,
    void *private_ctx UNUSED);

static int rpc_trigger_event_cb(
    sr_session_ctx_t *session UNUSED,
    uint32_t sub_id UNUSED,
    const char *path UNUSED,
    const sr_val_t *input UNUSED,
    const size_t input_cnt UNUSED,
    sr_event_t event UNUSED,
    uint32_t request_id UNUSED,
    sr_val_t **output UNUSED,
    size_t *output_cnt UNUSED,
    void *private_ctx UNUSED);

// ---------------------------------------------------------------------------
// RPC Callback implementations
// ---------------------------------------------------------------------------
static int rpc_get_system_status_cb(
    sr_session_ctx_t *session UNUSED,
    uint32_t sub_id UNUSED,
    const char *path UNUSED,
    const sr_val_t *input UNUSED,
    const size_t input_cnt UNUSED,
    sr_event_t event UNUSED,
    uint32_t request_id UNUSED,
    sr_val_t **output,
    size_t *output_cnt,
    void *private_ctx UNUSED)
{
    (void)session; (void)sub_id; (void)path; (void)input; (void)input_cnt;
    (void)event; (void)request_id; (void)private_ctx;
    
    SRPLG_LOG_DBG("restconf-test", "RPC get-system-status callback called");
    
    // Créer la réponse
    sr_val_t *vals = NULL;
    int rc = SR_ERR_OK;
    
    // Allouer 2 valeurs (status et timestamp)
    vals = calloc(2, sizeof(*vals));
    if (!vals) {
        SRPLG_LOG_ERR("restconf-test", "Memory allocation failed");
        return SR_ERR_OPERATION_FAILED;
    }
    
    // Statut du système
    vals[0].xpath = strdup("/restconf-test:output/status");
    vals[0].type = SR_STRING_T;
    vals[0].data.string_val = strdup("operational");
    
    // Timestamp
    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    vals[1].xpath = strdup("/restconf-test:output/timestamp");
    vals[1].type = SR_STRING_T;
    vals[1].data.string_val = strdup(timestamp);
    
    *output = vals;
    *output_cnt = 2;
    
    return rc;
}

static int rpc_configure_device_cb(
    sr_session_ctx_t *session UNUSED,
    uint32_t sub_id UNUSED,
    const char *path UNUSED,
    const sr_val_t *input,
    const size_t input_cnt UNUSED,
    sr_event_t event UNUSED,
    uint32_t request_id UNUSED,
    sr_val_t **output,
    size_t *output_cnt,
    void *private_ctx UNUSED)
{
    (void)session; (void)sub_id; (void)path; (void)input_cnt;
    (void)event; (void)request_id; (void)private_ctx;
    
    SRPLG_LOG_DBG("restconf-test", "RPC configure-device callback called");
    
    sr_val_t *vals = NULL;
    int rc = SR_ERR_OK;
    
    // Allouer 2 valeurs (result et device-id)
    vals = calloc(2, sizeof(*vals));
    if (!vals) {
        SRPLG_LOG_ERR("restconf-test", "Memory allocation failed");
        return SR_ERR_OPERATION_FAILED;
    }
    
    // Résultat de l'opération
    vals[0].xpath = strdup("/restconf-test:output/result");
    vals[0].type = SR_STRING_T;
    vals[0].data.string_val = strdup("success");
    
    // ID du device
    vals[1].xpath = strdup("/restconf-test:output/device-id");
    vals[1].type = SR_UINT32_T;
    vals[1].data.uint32_val = 12345;
    
    *output = vals;
    *output_cnt = 2;
    
    return rc;
}

static int rpc_create_resource_cb(
    sr_session_ctx_t *session UNUSED,
    uint32_t sub_id UNUSED,
    const char *path UNUSED,
    const sr_val_t *input,
    const size_t input_cnt UNUSED,
    sr_event_t event UNUSED,
    uint32_t request_id UNUSED,
    sr_val_t **output,
    size_t *output_cnt,
    void *private_ctx UNUSED)
{
    (void)session; (void)sub_id; (void)path; (void)input_cnt;
    (void)event; (void)request_id; (void)private_ctx;
    
    SRPLG_LOG_DBG("restconf-test", "RPC create-resource callback called");
    
    sr_val_t *vals = NULL;
    int rc = SR_ERR_OK;
    
    // Allouer 1 valeur (id)
    vals = calloc(1, sizeof(*vals));
    if (!vals) {
        SRPLG_LOG_ERR("restconf-test", "Memory allocation failed");
        return SR_ERR_OPERATION_FAILED;
    }
    
    // ID de la ressource créée
    vals[0].xpath = strdup("/restconf-test:output/id");
    vals[0].type = SR_UINT32_T;
    vals[0].data.uint32_val = 54321;
    
    *output = vals;
    *output_cnt = 1;
    
    return rc;
}

static int rpc_set_operation_mode_cb(
    sr_session_ctx_t *session UNUSED,
    uint32_t sub_id UNUSED,
    const char *path UNUSED,
    const sr_val_t *input,
    const size_t input_cnt UNUSED,
    sr_event_t event UNUSED,
    uint32_t request_id UNUSED,
    sr_val_t **output,
    size_t *output_cnt,
    void *private_ctx UNUSED)
{
    (void)session; (void)sub_id; (void)path; (void)input_cnt;
    (void)event; (void)request_id; (void)private_ctx;
    
    SRPLG_LOG_DBG("restconf-test", "RPC set-operation-mode callback called");
    
    sr_val_t *vals = NULL;
    int rc = SR_ERR_OK;
    
    // Allouer 1 valeur (previous-mode)
    vals = calloc(1, sizeof(*vals));
    if (!vals) {
        SRPLG_LOG_ERR("restconf-test", "Memory allocation failed");
        return SR_ERR_OPERATION_FAILED;
    }
    
    // Mode précédent (on retourne normal par défaut)
    vals[0].xpath = strdup("/restconf-test:output/previous-mode");
    vals[0].type = SR_ENUM_T;
    vals[0].data.enum_val = strdup("normal");
    
    *output = vals;
    *output_cnt = 1;
    
    return rc;
}

static int rpc_process_data_cb(
    sr_session_ctx_t *session UNUSED,
    uint32_t sub_id UNUSED,
    const char *path UNUSED,
    const sr_val_t *input UNUSED,
    const size_t input_cnt UNUSED,
    sr_event_t event UNUSED,
    uint32_t request_id UNUSED,
    sr_val_t **output,
    size_t *output_cnt,
    void *private_ctx UNUSED)
{
    (void)session; (void)sub_id; (void)path; (void)input; (void)input_cnt;
    (void)event; (void)request_id; (void)private_ctx;
    
    SRPLG_LOG_DBG("restconf-test", "RPC process-data callback called");
    
    sr_val_t *vals = NULL;
    int rc = SR_ERR_OK;
    
    // Allouer 1 valeur (processed)
    vals = calloc(1, sizeof(*vals));
    if (!vals) {
        SRPLG_LOG_ERR("restconf-test", "Memory allocation failed");
        return SR_ERR_OPERATION_FAILED;
    }
    
    // Indique que les données ont été traitées
    vals[0].xpath = strdup("/restconf-test:output/processed");
    vals[0].type = SR_BOOL_T;
    vals[0].data.bool_val = true;
    
    *output = vals;
    *output_cnt = 1;
    
    return rc;
}

static int rpc_trigger_event_cb(
    sr_session_ctx_t *session UNUSED,
    uint32_t sub_id UNUSED,
    const char *path UNUSED,
    const sr_val_t *input UNUSED,
    const size_t input_cnt UNUSED,
    sr_event_t event UNUSED,
    uint32_t request_id UNUSED,
    sr_val_t **output UNUSED,
    size_t *output_cnt UNUSED,
    void *private_ctx UNUSED)
{
    (void)session; (void)sub_id; (void)path; (void)input; (void)input_cnt;
    (void)event; (void)request_id; (void)output; (void)output_cnt; (void)private_ctx;
    
    SRPLG_LOG_DBG("restconf-test", "RPC trigger-event callback called");
    
    // Pas de sortie pour ce RPC
    return SR_ERR_OK;
}

// ---------------------------------------------------------------------------
// Plugin initialization
// ---------------------------------------------------------------------------

/**
 * Point d'entree du plugin (appele par sysrepod)
 */
int sr_plugin_init_cb(sr_session_ctx_t *session, void **private_ctx)
{
    test_plugin_ctx_t *ctx = NULL;
    int rc = SR_ERR_OK;
    
    SRPLG_LOG_DBG("restconf-test", "Initializing restconf-test plugin");
    
    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        SRPLG_LOG_ERR("restconf-test", "Failed to allocate plugin context");
        return SR_ERR_OPERATION_FAILED;
    }
    
    ctx->connection = sr_session_get_connection(session);
    ctx->session = session;
    
    // S'abonner a tous les RPCs du module avec des callbacks spécifiques
    sr_subscription_ctx_t *sub = NULL;
    
    rc = sr_rpc_subscribe(session, "/restconf-test:get-system-status", rpc_get_system_status_cb, ctx, 0, SR_SUBSCR_NO_THREAD, &sub);
    if (rc != SR_ERR_OK) { SRPLG_LOG_ERR("restconf-test", "RPC get-system-status subscribe failed: %s", sr_strerror(rc)); goto error; }
    
    rc = sr_rpc_subscribe(session, "/restconf-test:configure-device", rpc_configure_device_cb, ctx, 0, SR_SUBSCR_NO_THREAD, &sub);
    if (rc != SR_ERR_OK) { SRPLG_LOG_ERR("restconf-test", "RPC configure-device subscribe failed: %s", sr_strerror(rc)); goto error; }
    
    rc = sr_rpc_subscribe(session, "/restconf-test:create-resource", rpc_create_resource_cb, ctx, 0, SR_SUBSCR_NO_THREAD, &sub);
    if (rc != SR_ERR_OK) { SRPLG_LOG_ERR("restconf-test", "RPC create-resource subscribe failed: %s", sr_strerror(rc)); goto error; }
    
    rc = sr_rpc_subscribe(session, "/restconf-test:set-operation-mode", rpc_set_operation_mode_cb, ctx, 0, SR_SUBSCR_NO_THREAD, &sub);
    if (rc != SR_ERR_OK) { SRPLG_LOG_ERR("restconf-test", "RPC set-operation-mode subscribe failed: %s", sr_strerror(rc)); goto error; }
    
    rc = sr_rpc_subscribe(session, "/restconf-test:process-data", rpc_process_data_cb, ctx, 0, SR_SUBSCR_NO_THREAD, &sub);
    if (rc != SR_ERR_OK) { SRPLG_LOG_ERR("restconf-test", "RPC process-data subscribe failed: %s", sr_strerror(rc)); goto error; }
    
    rc = sr_rpc_subscribe(session, "/restconf-test:trigger-event", rpc_trigger_event_cb, ctx, 0, SR_SUBSCR_NO_THREAD, &sub);
    if (rc != SR_ERR_OK) { SRPLG_LOG_ERR("restconf-test", "RPC trigger-event subscribe failed: %s", sr_strerror(rc)); goto error; }
    
    *private_ctx = ctx;
    SRPLG_LOG_DBG("restconf-test", "restconf-test plugin initialized successfully");
    return SR_ERR_OK;
    
error:
    free(ctx);
    return rc;
}

/**
 * Cleanup callback (appele par sysrepod)
 */
void sr_plugin_cleanup_cb(sr_session_ctx_t *session UNUSED, void *private_ctx)
{
    test_plugin_ctx_t *ctx = (test_plugin_ctx_t *)private_ctx;
    (void)session;
    
    if (!ctx) return;
    
    SRPLG_LOG_DBG("restconf-test", "Cleaning up restconf-test plugin");
    
    if (ctx->subscription) {
        sr_unsubscribe(ctx->subscription);
    }
    
    free(ctx);
}
