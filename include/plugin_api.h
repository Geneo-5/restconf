#ifndef PLUGIN_API_H
#define PLUGIN_API_H

#include <stdint.h>
#include <stdbool.h>
#include <sysrepo.h>
#include "router.h"

struct event_base; /* Forward declaration */

typedef struct plugin_ctx_s plugin_ctx_t;

/**
 * @brief Callback de résultat pour une opération GET/RPC.
 *
 * Contrairement à l'ancienne API (qui exposait un sr_data_t* lié à
 * une connexion sysrepo locale), ce callback reçoit directement la
 * réponse RESTCONF finale (déjà sérialisée en JSON/XML, filtrage
 * "fields" et "with-defaults" déjà appliqués) : c'est ce qui permet
 * au mode Externe (IPC UDS) de fonctionner, puisque le processus
 * gateway n'a alors aucun accès direct à sysrepo/libyang.
 *
 * @param http_status Code de statut HTTP à renvoyer au client.
 * @param body Corps de la réponse (JSON/XML), à libérer par
 *             l'appelant du callback via free(). NULL si vide
 *             (ex: 204 No Content).
 * @param body_len Taille de @p body en octets.
 */
typedef void (*plugin_data_cb)(
	int http_status, uint8_t *body, size_t body_len,
	const char *etag,
	void *user_data);
typedef void (*plugin_rpc_cb)(
	int http_status, uint8_t *body, size_t body_len,
	void *user_data);
typedef void (*plugin_status_cb)(
	int error_code, void *user_data);

plugin_ctx_t *plugin_init(
	struct event_base *base, bool use_external,
	const char *uds_path);

void plugin_handle_get(
	plugin_ctx_t *ctx, const rc_request_t *req,
	plugin_data_cb callback, void *user_data);

void plugin_handle_rpc(
	plugin_ctx_t *ctx,
	const rc_request_t *req,
	const uint8_t *body,
	size_t body_len,
	plugin_rpc_cb callback,
	void *user_data);

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

typedef void (*plugin_edit_cb)(
	int http_status,
	const char *error_tag,
	const char *error_msg,
	const char *etag,
	void *user_data);

void plugin_handle_edit(
	plugin_ctx_t *ctx,
	const rc_request_t *req,
	const uint8_t *body,
	size_t body_len,
	plugin_edit_cb callback,
	void *user_data);

/* Forward declaration from uds_plugin.c */
extern int ext_plugin_init_uds(
	struct event_base *base, const char *path,
	plugin_ctx_t *sr_ctx);

#endif /* PLUGIN_API_H */