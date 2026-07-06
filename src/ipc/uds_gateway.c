#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include "plugin_api.h"
#include "ipc/uds_common.h"

struct plugin_ctx_s {
		struct event_base *base;
		struct bufferevent *bev;
		/* TODO: Add pending requests map */
};

static void uds_read_cb(
		struct bufferevent *bev, void *ctx)
{
		/* TODO: Read IPC messages, parse header,
		 * dispatch to pending request callbacks */
}

static void uds_event_cb(
		struct bufferevent *bev, short events, void *ctx)
{
		if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
				/* TODO: Handle disconnect, attempt reconnect */
		}
}

plugin_ctx_t *plugin_init(
		struct event_base *base,
		bool use_external,
		const char *uds_path)
{
		if (!use_external) {
				/* Handled in sysrepo_plugin.c */
				return NULL;
		}

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
		plugin_ctx_t *ctx,
		const rc_request_t *req,
		plugin_data_cb callback,
		void *user_data)
{
		/* TODO: Serialize request, send via bev,
		 * register callback for response */
}

void plugin_handle_edit(
		plugin_ctx_t *ctx,
		const rc_request_t *req,
		plugin_status_cb callback,
		void *user_data)
{
		/* TODO: Serialize and send */
}

void plugin_handle_rpc(
		plugin_ctx_t *ctx,
		const rc_request_t *req,
		plugin_rpc_cb callback,
		void *user_data)
{
		/* TODO: Serialize and send */
}

void plugin_subscribe_notifications(
		plugin_ctx_t *ctx,
		plugin_notif_cb callback,
		void *user_data)
{
		/* TODO: Send subscription request via UDS */
}

void plugin_destroy(plugin_ctx_t *ctx)
{
		if (!ctx) return;
		if (ctx->bev) bufferevent_free(ctx->bev);
		free(ctx);
}