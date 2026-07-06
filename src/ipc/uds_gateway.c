#define _GNU_SOURCE
#define UNUSED __attribute__((unused))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <libyang/libyang.h> /* Nécessaire pour struct ly_ctx */
#include "plugin_api.h"
#include "ipc/uds_common.h"

struct plugin_ctx_s {
	struct event_base *base;
	struct bufferevent *bev;
};

static void uds_read_cb(
	struct bufferevent *bev UNUSED, void *ctx UNUSED)
{
	/* TODO: Read IPC messages, parse header,
	 * dispatch to pending request callbacks */
}

static void uds_event_cb(
	struct bufferevent *bev UNUSED,
	short events UNUSED, void *ctx UNUSED)
{
	/* TODO: Handle disconnect, attempt reconnect */
}

plugin_ctx_t *plugin_init(
	struct event_base *base, bool use_external UNUSED,
	const char *uds_path UNUSED)
{
	plugin_ctx_t *ctx = calloc(1, sizeof(plugin_ctx_t));
	ctx->base = base;

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) { free(ctx); return NULL; }

	struct sockaddr_un addr = { 0 };
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, uds_path,
		sizeof(addr.sun_path) - 1);

	if (connect(fd, (struct sockaddr *)&addr,
		sizeof(addr)) < 0) {
		close(fd);
		free(ctx);
		return NULL;
	}

	ctx->bev = bufferevent_socket_new(
		base, fd, BEV_OPT_CLOSE_ON_FREE);
	bufferevent_setcb(
		ctx->bev, uds_read_cb, NULL,
		uds_event_cb, ctx);
	bufferevent_enable(ctx->bev, EV_READ | EV_WRITE);

	return ctx;
}

void plugin_handle_get(
	plugin_ctx_t *ctx UNUSED,
	const rc_request_t *req UNUSED,
	plugin_data_cb callback UNUSED,
	void *user_data UNUSED)
{
	/* TODO: Serialize request, send via bev */
}

void plugin_handle_edit(
	plugin_ctx_t *ctx UNUSED,
	const rc_request_t *req UNUSED,
	const uint8_t *body UNUSED,
	size_t body_len UNUSED,
	plugin_edit_cb callback UNUSED,
	void *user_data UNUSED)
{
	/* TODO: Serialize and send */
}

void plugin_handle_rpc(
	plugin_ctx_t *ctx UNUSED,
	const rc_request_t *req UNUSED,
	plugin_rpc_cb callback UNUSED,
	void *user_data UNUSED)
{
	/* TODO: Serialize and send */
}

void plugin_subscribe_notifications(
	plugin_ctx_t *ctx UNUSED,
	plugin_notif_cb callback UNUSED,
	void *user_data UNUSED)
{
	/* TODO: Send subscription request via UDS */
}

void plugin_destroy(plugin_ctx_t *ctx)
{
	if (!ctx) return;
	if (ctx->bev) bufferevent_free(ctx->bev);
	free(ctx);
}

/* ======================================================================
 * Gestion du contexte libyang pour le mode Externe
 * ====================================================================== */

const struct ly_ctx *plugin_acquire_ly_ctx(plugin_ctx_t *ctx)
{
	/* En mode externe, le gateway n'a pas d'accès direct à sysrepo.
	 * On retourne NULL. Le routeur devra fonctionner sans contexte
	 * (la résolution des clés de listes sera désactivée).
	 * TODO: Implémenter un ly_ctx local dans le gateway ou le
	 * récupérer via IPC. */
	(void)ctx;
	return NULL;
}

void plugin_release_ly_ctx(plugin_ctx_t *ctx)
{
	/* Rien à libérer puisque nous n'avons pas acquis de contexte */
	(void)ctx;
}