#ifndef PLUGIN_API_H
#define PLUGIN_API_H

#include <stdint.h>
#include <stdbool.h>
#include <sysrepo.h>
#include "router.h"

struct event_base; /* Forward declaration */

typedef struct plugin_ctx_s plugin_ctx_t;

typedef void (*plugin_data_cb)(
	sr_data_t *data, int error_code, void *user_data);
typedef void (*plugin_rpc_cb)(
	sr_data_t *output, int error_code, void *user_data);
typedef void (*plugin_status_cb)(
	int error_code, void *user_data);

plugin_ctx_t *plugin_init(
	struct event_base *base, bool use_external,
	const char *uds_path);

void plugin_handle_get(
	plugin_ctx_t *ctx, const rc_request_t *req,
	plugin_data_cb callback, void *user_data);

void plugin_handle_edit(
	plugin_ctx_t *ctx, const rc_request_t *req,
	plugin_status_cb callback, void *user_data);

void plugin_handle_rpc(
	plugin_ctx_t *ctx, const rc_request_t *req,
	plugin_rpc_cb callback, void *user_data);

typedef void (*plugin_notif_cb)(
	const char *module_name, const char *xpath,
	const char *payload, void *user_data);

void plugin_subscribe_notifications(
	plugin_ctx_t *ctx, plugin_notif_cb callback,
	void *user_data);

void plugin_destroy(plugin_ctx_t *ctx);

/**
 * @brief Acquiert le contexte libyang de manière thread-safe.
 * Doit être libéré avec plugin_release_ly_ctx().
 */
const struct ly_ctx *plugin_acquire_ly_ctx(plugin_ctx_t *ctx);

/**
 * @brief Libère le contexte libyang précédemment acquis.
 */
void plugin_release_ly_ctx(plugin_ctx_t *ctx);

#endif /* PLUGIN_API_H */