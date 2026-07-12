/**
 * @file sysrepo_worker.h
 * @brief Sysrepo worker thread - confined blocking boundary
 *        (ROADMAP.md item 3.12).
 *
 * All blocking sysrepo calls (sr_get_data, sr_apply_changes,
 * sr_rpc_send_tree) are confined to a single dedicated pthread.
 * The libevent thread communicates with it exclusively through
 * a message queue (request direction) and a completion queue
 * notified by an eventfd (response direction).
 *
 * The sysrepo connection (sr_conn_ctx_t) is owned by the plugin
 * (sysrepo_plugin.c) and shared with the worker.  The worker
 * creates its own sessions on this connection for GET/EDIT/RPC
 * operations.  Subscriptions (oper_get, rpc_subscribe_tree,
 * notif_subscribe) are handled by the plugin in the libevent
 * thread with SR_SUBSCR_NO_THREAD.
 */
#ifndef SYSREPO_WORKER_H
#define SYSREPO_WORKER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <sysrepo.h>
#include <libyang/libyang.h>
#include "plugin_api.h"
#include "router.h"

struct event_base;

typedef struct sr_worker_s sr_worker_t;

/**
 * @brief Create and start the sysrepo worker thread.
 *
 * @param[in] base libevent event base for completion
 *            notifications.
 * @param[in] conn Shared sysrepo connection (owned by plugin).
 *            The worker creates its own sessions on this
 *            connection for GET/EDIT/RPC operations.
 * @return Worker handle, or NULL on error.
 */
sr_worker_t *sr_worker_create(
	struct event_base *base, sr_conn_ctx_t *conn);

/**
 * @brief Stop and destroy the worker thread.
 *
 * Blocks until the worker thread has exited.  All pending
 * requests are completed before return.
 *
 * @param[in] worker Worker handle (may be NULL).
 */
void sr_worker_destroy(sr_worker_t *worker);

/**
 * @brief Submit a GET operation to the worker.
 *
 * Returns immediately; @p callback is invoked later from
 * the libevent thread.
 *
 * @param[in] worker      Worker handle.
 * @param[in] res_type    RC_RES_DATA or RC_RES_DS.
 * @param[in] datastore   Target NMDA datastore.
 * @param[in] username    NACM identity (deep-copied).
 * @param[in] xpath       XPath query (deep-copied).
 * @param[in] content_filter  "config"/"nonconfig"/NULL.
 * @param[in] depth       Max depth (-1 = unbounded).
 * @param[in] fields_expr fields filter (deep-copied).
 * @param[in] with_defaults with-defaults mode string.
 * @param[in] with_origin NMDA with-origin flag.
 * @param[in] accept_type JSON or XML.
 * @param[in] callback    Completion callback.
 * @param[in] user_data   Opaque pointer for callback.
 */
void sr_worker_submit_get(
	sr_worker_t *worker,
	rc_resource_type_t res_type,
	rc_datastore_t datastore,
	const char *username,
	const char *xpath,
	const char *content_filter,
	int depth,
	const char *fields_expr,
	const char *with_defaults,
	bool with_origin,
	media_type_t accept_type,
	plugin_data_cb callback,
	void *user_data);

/**
 * @brief Submit an EDIT operation (POST/PUT/PATCH/DELETE).
 *
 * @param[in] worker     Worker handle.
 * @param[in] datastore  Target datastore.
 * @param[in] username   NACM identity.
 * @param[in] xpath      Target XPath.
 * @param[in] method     HTTP method string.
 * @param[in] req_type   Content-Type.
 * @param[in] accept_type Accept header type.
 * @param[in] if_match   If-Match header (ETag).
 * @param[in] body       Request body (deep-copied).
 * @param[in] body_len   Body length.
 * @param[in] callback   Completion callback.
 * @param[in] user_data  Opaque pointer.
 */
void sr_worker_submit_edit(
	sr_worker_t *worker,
	rc_datastore_t datastore,
	const char *username,
	const char *xpath,
	const char *method,
	media_type_t req_type,
	media_type_t accept_type,
	const char *if_match,
	const uint8_t *body,
	size_t body_len,
	plugin_edit_cb callback,
	void *user_data);

/**
 * @brief Submit an RPC/action invocation.
 *
 * @param[in] worker     Worker handle.
 * @param[in] username   NACM identity.
 * @param[in] rpc_module YANG module name.
 * @param[in] rpc_name   RPC/action name.
 * @param[in] req_type   Content-Type.
 * @param[in] accept_type Accept type.
 * @param[in] body       RPC input body (deep-copied).
 * @param[in] body_len   Body length.
 * @param[in] callback   Completion callback.
 * @param[in] user_data  Opaque pointer.
 */
void sr_worker_submit_rpc(
	sr_worker_t *worker,
	const char *username,
	const char *rpc_module,
	const char *rpc_name,
	media_type_t req_type,
	media_type_t accept_type,
	const uint8_t *body,
	size_t body_len,
	plugin_rpc_cb callback,
	void *user_data);

/**
 * @brief Acquire libyang context (thread-safe per sysrepo).
 */
const struct ly_ctx *sr_worker_acquire_ly_ctx(
	sr_worker_t *worker);

/**
 * @brief Release libyang context.
 */
void sr_worker_release_ly_ctx(sr_worker_t *worker);

#endif /* SYSREPO_WORKER_H */
