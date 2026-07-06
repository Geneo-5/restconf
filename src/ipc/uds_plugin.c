#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include "ipc/uds_common.h"

/* Context for the external plugin daemon */
typedef struct {
		struct event_base *base;
		struct evconnlistener *listener;
		/* TODO: Add sysrepo session, etc. */
} ext_plugin_ctx_t;

static void gateway_read_cb(
		struct bufferevent *bev, void *ctx)
{
		/* TODO: Read IPC header, read payload,
		 * dispatch to internal sysrepo functions,
		 * serialize response and write back to bev */
}

static void gateway_event_cb(
		struct bufferevent *bev, short events, void *ctx)
{
		if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
				bufferevent_free(bev);
		}
}

static void accept_gateway_cb(
		struct evconnlistener *listener,
		evutil_socket_t fd,
		struct sockaddr *address,
		int socklen,
		void *ctx)
{
		ext_plugin_ctx_t *pctx = (ext_plugin_ctx_t *)ctx;
		struct bufferevent *bev = bufferevent_socket_new(
				pctx->base, fd, BEV_OPT_CLOSE_ON_FREE);

		bufferevent_setcb(
				bev, gateway_read_cb, NULL,
				gateway_event_cb, pctx);
		bufferevent_enable(bev, EV_READ | EV_WRITE);
}

int ext_plugin_init_uds(
		struct event_base *base,
		const char *uds_path)
{
		ext_plugin_ctx_t *ctx = calloc(
				1, sizeof(ext_plugin_ctx_t));
		ctx->base = base;

		unlink(uds_path); /* Remove old socket */

		struct sockaddr_un addr = { 0 };
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, uds_path,
		        sizeof(addr.sun_path) - 1);

		ctx->listener = evconnlistener_new_bind(
				base, accept_gateway_cb, ctx,
				LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE,
				-1, (struct sockaddr *)&addr,
				sizeof(addr));

		if (!ctx->listener) {
				free(ctx);
				return -1;
		}
		return 0;
}