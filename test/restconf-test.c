/**
 * restconf-test.c - Sysrepo plugin for the restconf-test.yang module
 *
 * External plugin loaded by sysrepo-plugind.
 * Handles RPCs for the restconf-test.yang module using the
 * tree-based API (sr_rpc_subscribe_tree / sr_rpc_send_tree)
 * which is what the RESTCONF server uses.
 *
 * Configuration data is managed directly by sysrepo.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sysrepo.h>
#include <libyang/libyang.h>

#ifndef UNUSED
#define UNUSED __attribute__((unused))
#endif

#define PLUGIN_NAME "restconf-test-plugin"
#define MODULE_NAME "restconf-test"

extern int sr_plugin_init_cb(sr_session_ctx_t *session, void **private_ctx);
extern void sr_plugin_cleanup_cb(sr_session_ctx_t *session UNUSED, void *private_ctx);

typedef struct test_plugin_ctx_s {
	sr_conn_ctx_t *connection;
	sr_session_ctx_t *session;
	sr_subscription_ctx_t *subscription;
} test_plugin_ctx_t;

/* -------------------------------------------------------------------
 * Tree-based RPC callback declarations
 * ------------------------------------------------------------------- */
static int rpc_get_system_status_cb(
	sr_session_ctx_t *session,
	uint32_t sub_id UNUSED,
	const char *op_path UNUSED,
	const struct lyd_node *input UNUSED,
	sr_event_t event UNUSED,
	uint32_t request_id UNUSED,
	struct lyd_node *output,
	void *private_data UNUSED);

static int rpc_configure_device_cb(
	sr_session_ctx_t *session,
	uint32_t sub_id UNUSED,
	const char *op_path UNUSED,
	const struct lyd_node *input UNUSED,
	sr_event_t event UNUSED,
	uint32_t request_id UNUSED,
	struct lyd_node *output,
	void *private_data UNUSED);

static int rpc_create_resource_cb(
	sr_session_ctx_t *session,
	uint32_t sub_id UNUSED,
	const char *op_path UNUSED,
	const struct lyd_node *input,
	sr_event_t event UNUSED,
	uint32_t request_id UNUSED,
	struct lyd_node *output,
	void *private_data UNUSED);

static int rpc_set_operation_mode_cb(
	sr_session_ctx_t *session,
	uint32_t sub_id UNUSED,
	const char *op_path UNUSED,
	const struct lyd_node *input UNUSED,
	sr_event_t event UNUSED,
	uint32_t request_id UNUSED,
	struct lyd_node *output,
	void *private_data UNUSED);

static int rpc_process_data_cb(
	sr_session_ctx_t *session,
	uint32_t sub_id UNUSED,
	const char *op_path UNUSED,
	const struct lyd_node *input UNUSED,
	sr_event_t event UNUSED,
	uint32_t request_id UNUSED,
	struct lyd_node *output,
	void *private_data UNUSED);

static int rpc_trigger_event_cb(
	sr_session_ctx_t *session,
	uint32_t sub_id UNUSED,
	const char *op_path UNUSED,
	const struct lyd_node *input UNUSED,
	sr_event_t event UNUSED,
	uint32_t request_id UNUSED,
	struct lyd_node *output UNUSED,
	void *private_data UNUSED);

/* -------------------------------------------------------------------
 * Tree-based RPC callback implementations
 * ------------------------------------------------------------------- */
static int
rpc_get_system_status_cb(
	sr_session_ctx_t *session,
	uint32_t sub_id UNUSED,
	const char *op_path UNUSED,
	const struct lyd_node *input UNUSED,
	sr_event_t event UNUSED,
	uint32_t request_id UNUSED,
	struct lyd_node *output,
	void *private_data UNUSED)
{
	const struct ly_ctx *ctx;

	(void)sub_id; (void)op_path; (void)input;
	(void)event; (void)request_id; (void)private_data;

	SRPLG_LOG_DBG(MODULE_NAME,
	              "RPC get-system-status (tree) called");

	ctx = sr_acquire_context(
		sr_session_get_connection(session));
	if (!ctx) return SR_ERR_OPERATION_FAILED;

	time_t now = time(NULL);
	char ts[32];

	strftime(ts, sizeof(ts),
	         "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

	lyd_new_term(output, NULL, "status",
	             "operational", 0, NULL);
	lyd_new_term(output, NULL, "timestamp", ts, 0, NULL);

	sr_release_context(sr_session_get_connection(session));
	return SR_ERR_OK;
}

static int
rpc_configure_device_cb(
	sr_session_ctx_t *session,
	uint32_t sub_id UNUSED,
	const char *op_path UNUSED,
	const struct lyd_node *input UNUSED,
	sr_event_t event UNUSED,
	uint32_t request_id UNUSED,
	struct lyd_node *output,
	void *private_data UNUSED)
{
	const struct ly_ctx *ctx;

	(void)sub_id; (void)op_path; (void)input;
	(void)event; (void)request_id; (void)private_data;

	SRPLG_LOG_DBG(MODULE_NAME,
	              "RPC configure-device (tree) called");

	ctx = sr_acquire_context(
		sr_session_get_connection(session));
	if (!ctx) return SR_ERR_OPERATION_FAILED;

	lyd_new_term(output, NULL, "result",
	             "success", 0, NULL);
	lyd_new_term(output, NULL, "device-id",
	             "12345", 0, NULL);

	sr_release_context(sr_session_get_connection(session));
	return SR_ERR_OK;
}

static int
rpc_create_resource_cb(
	sr_session_ctx_t *session,
	uint32_t sub_id UNUSED,
	const char *op_path UNUSED,
	const struct lyd_node *input,
	sr_event_t event UNUSED,
	uint32_t request_id UNUSED,
	struct lyd_node *output,
	void *private_data UNUSED)
{
	const struct ly_ctx *ctx;
	const struct lyd_node *child;

	(void)sub_id; (void)op_path;
	(void)event; (void)request_id; (void)private_data;

	SRPLG_LOG_DBG(MODULE_NAME,
	              "RPC create-resource (tree) called");

	/* Validate mandatory 'name' parameter */
	child = lyd_child(input);
	while (child) {
		if (child->schema &&
		    strcmp(child->schema->name, "name") == 0) {
			const char *v = lyd_get_value(child);
			if (!v || !*v) {
				sr_session_set_error_message(
					session,
					"Missing mandatory "
					"parameter 'name'");
				return SR_ERR_INVAL_ARG;
			}
			break;
		}
		child = child->next;
	}

	ctx = sr_acquire_context(
		sr_session_get_connection(session));
	if (!ctx) return SR_ERR_OPERATION_FAILED;

	lyd_new_term(output, NULL, "id", "54321", 0, NULL);

	sr_release_context(sr_session_get_connection(session));
	return SR_ERR_OK;
}

static int
rpc_set_operation_mode_cb(
	sr_session_ctx_t *session,
	uint32_t sub_id UNUSED,
	const char *op_path UNUSED,
	const struct lyd_node *input UNUSED,
	sr_event_t event UNUSED,
	uint32_t request_id UNUSED,
	struct lyd_node *output,
	void *private_data UNUSED)
{
	const struct ly_ctx *ctx;

	(void)sub_id; (void)op_path; (void)input;
	(void)event; (void)request_id; (void)private_data;

	SRPLG_LOG_DBG(MODULE_NAME,
	              "RPC set-operation-mode (tree) called");

	ctx = sr_acquire_context(
		sr_session_get_connection(session));
	if (!ctx) return SR_ERR_OPERATION_FAILED;

	lyd_new_term(output, NULL, "previous-mode",
	             "normal", 0, NULL);

	sr_release_context(sr_session_get_connection(session));
	return SR_ERR_OK;
}

static int
rpc_process_data_cb(
	sr_session_ctx_t *session,
	uint32_t sub_id UNUSED,
	const char *op_path UNUSED,
	const struct lyd_node *input UNUSED,
	sr_event_t event UNUSED,
	uint32_t request_id UNUSED,
	struct lyd_node *output,
	void *private_data UNUSED)
{
	const struct ly_ctx *ctx;

	(void)sub_id; (void)op_path; (void)input;
	(void)event; (void)request_id; (void)private_data;

	SRPLG_LOG_DBG(MODULE_NAME,
	              "RPC process-data (tree) called");

	ctx = sr_acquire_context(
		sr_session_get_connection(session));
	if (!ctx) return SR_ERR_OPERATION_FAILED;

	lyd_new_term(output, NULL, "processed",
	             "true", 0, NULL);

	sr_release_context(sr_session_get_connection(session));
	return SR_ERR_OK;
}

static int
rpc_trigger_event_cb(
	sr_session_ctx_t *session,
	uint32_t sub_id UNUSED,
	const char *op_path UNUSED,
	const struct lyd_node *input UNUSED,
	sr_event_t event UNUSED,
	uint32_t request_id UNUSED,
	struct lyd_node *output UNUSED,
	void *private_data UNUSED)
{
	(void)session; (void)sub_id; (void)op_path;
	(void)input; (void)event; (void)request_id;
	(void)output; (void)private_data;

	SRPLG_LOG_DBG(MODULE_NAME,
	              "RPC trigger-event (tree) called");

	/* No output for this RPC */
	return SR_ERR_OK;
}

/* -------------------------------------------------------------------
 * Plugin entry point
 * ------------------------------------------------------------------- */

/**
 * Entry point called by sysrepo-plugind.
 *
 * Uses sr_rpc_subscribe_tree (tree-based) so that the RESTCONF
 * server's sr_rpc_send_tree() calls reach our callbacks.
 */
int
sr_plugin_init_cb(sr_session_ctx_t *session, void **private_ctx)
{
	test_plugin_ctx_t *ctx = NULL;
	sr_subscription_ctx_t *sub = NULL;
	int rc = SR_ERR_OK;

	SRPLG_LOG_DBG(MODULE_NAME,
	              "Initializing restconf-test plugin (tree API)");

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		SRPLG_LOG_ERR(MODULE_NAME,
		              "Failed to allocate plugin context");
		return SR_ERR_OPERATION_FAILED;
	}

	ctx->connection = sr_session_get_connection(session);
	ctx->session = session;

#define SUB_RPC(NAME, CB) \
	rc = sr_rpc_subscribe_tree( \
		session, \
		"/" MODULE_NAME ":" NAME, \
		CB, ctx, 0, \
		SR_SUBSCR_NO_THREAD, &sub); \
	if (rc != SR_ERR_OK) { \
		SRPLG_LOG_ERR(MODULE_NAME, \
		              "Subscribe " NAME " failed: %s", \
		              sr_strerror(rc)); \
		goto error; \
	}

	SUB_RPC("get-system-status",
	        rpc_get_system_status_cb);
	SUB_RPC("configure-device",
	        rpc_configure_device_cb);
	SUB_RPC("create-resource",
	        rpc_create_resource_cb);
	SUB_RPC("set-operation-mode",
	        rpc_set_operation_mode_cb);
	SUB_RPC("process-data",
	        rpc_process_data_cb);
	SUB_RPC("trigger-event",
	        rpc_trigger_event_cb);

#undef SUB_RPC

	ctx->subscription = sub;
	*private_ctx = ctx;

	SRPLG_LOG_DBG(MODULE_NAME,
	              "restconf-test plugin initialized (tree API)");
	return SR_ERR_OK;

error:
	if (sub) sr_unsubscribe(sub);
	free(ctx);
	return rc;
}

/**
 * Cleanup callback called by sysrepo-plugind on shutdown.
 */
void
sr_plugin_cleanup_cb(
	sr_session_ctx_t *session UNUSED, void *private_ctx)
{
	test_plugin_ctx_t *ctx = (test_plugin_ctx_t *)private_ctx;
	(void)session;

	if (!ctx) return;

	SRPLG_LOG_DBG(MODULE_NAME,
	              "Cleaning up restconf-test plugin");

	if (ctx->subscription)
		sr_unsubscribe(ctx->subscription);

	free(ctx);
}
