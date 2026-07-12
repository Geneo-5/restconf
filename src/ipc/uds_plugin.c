#define _GNU_SOURCE
#define UNUSED __attribute__((unused))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include "ipc/uds_common.h"
#include "ipc/uds_data_proto.h"
#include "plugin_api.h"
#include "router.h"
#include "logger.h"

/*
 * Mode Externe (IPC UDS) - cote Daemon (restconf-plugin).
 *
 * Recoit les requetes IPC_MSG_DATA_REQ/EDIT_REQ/RPC_REQ envoyees par
 * uds_gateway.c, les deserialise en un rc_request_t local, et les
 * processes by reusing the internal-mode sysrepo code
 * (plugin_handle_get/edit/rpc, voir sysrepo_plugin.c) : ce daemon
 * est simplement un second appelant de la meme API plugin_api.h,
 * avec une connexion sysrepo qui lui est propre (voir plugin_main.c
 * qui appelle plugin_init()).
 */

typedef struct {
	struct event_base *base;
	struct evconnlistener *listener;
	plugin_ctx_t *sr_ctx; /* Contexte sysrepo interne partage */
	/* ROADMAP.md item 6.1 : gateways actuellement connectes sur
	 * l'UDS, pour diffusion des notifications sysrepo poussees
	 * (IPC_MSG_NOTIF_PUSH, cf. daemon_notif_push_cb() plus bas). */
	struct gw_conn_s *gw_conns;
} ext_plugin_ctx_t;

typedef struct gw_conn_s {
	struct bufferevent *bev;
	struct gw_conn_s *next;
} gw_conn_t;

/* Contexte porte le temps de router la reponse (bien que le
 * traitement sysrepo soit synchrone) vers la bonne connexion UDS et
 * le bon msg_id. */
typedef struct {
	struct bufferevent *bev;
	uint32_t msg_id;
} dispatch_ctx_t;

static void send_ipc_response(
	struct bufferevent *bev, ipc_msg_type_t type,
	uint32_t msg_id, int32_t status_code,
	const uint8_t *payload, size_t payload_len)
{
	uint8_t *buf = NULL;
	size_t buf_len = 0;

	if (ipc_serialize_message(
		type, msg_id, status_code,
		payload, payload_len, &buf, &buf_len) != 0) {
		RC_ERROR("uds-plugin: failed to serialize response");
		return;
	}
	bufferevent_write(bev, buf, buf_len);
	free(buf);
}

static void on_data_response(
	int http_status, uint8_t *body, size_t body_len,
	const char *etag,
	void *user_data)
{
	dispatch_ctx_t *dctx = (dispatch_ctx_t *)user_data;

	(void)etag; /* ETag computed by gateway from body */

	send_ipc_response(
		dctx->bev, IPC_MSG_DATA_RES, dctx->msg_id,
		http_status, body, body_len);

	if (body) free(body);
	free(dctx);
}

static void on_edit_response(
	int http_status, const char *error_tag,
	const char *error_msg, const char *etag,
	void *user_data)
{
	dispatch_ctx_t *dctx = (dispatch_ctx_t *)user_data;
	uint8_t payload[1024];
	size_t pos = 0;

	(void)etag; /* Not yet forwarded in IPC protocol */

	uds_proto_put_str(payload, sizeof(payload), &pos, error_tag);
	uds_proto_put_str(payload, sizeof(payload), &pos, error_msg);

	send_ipc_response(
		dctx->bev, IPC_MSG_EDIT_RES, dctx->msg_id,
		http_status, payload, pos);

	free(dctx);
}

static void on_rpc_response(
	int http_status, uint8_t *body, size_t body_len,
	void *user_data)
{
	dispatch_ctx_t *dctx = (dispatch_ctx_t *)user_data;

	send_ipc_response(
		dctx->bev, IPC_MSG_RPC_RES, dctx->msg_id,
		http_status, body, body_len);

	if (body) free(body);
	free(dctx);
}

static void handle_data_req(
	ext_plugin_ctx_t *pctx, struct bufferevent *bev,
	uint32_t msg_id, const uint8_t *payload, size_t len)
{
	size_t pos = 0;
	rc_request_t req = { 0 };
	uint32_t ds = 0, accept = 0;
	int32_t depth = -1;
	uint8_t with_origin = 0;
	dispatch_ctx_t *dctx;

	if (uds_proto_get_u32(payload, len, &pos, &ds) != 0 ||
	    uds_proto_get_i32(payload, len, &pos, &depth) != 0 ||
	    uds_proto_get_u8(payload, len, &pos, &with_origin) != 0 ||
	    uds_proto_get_u32(payload, len, &pos, &accept) != 0 ||
	    uds_proto_get_str(payload, len, &pos, &req.xpath) != 0 ||
	    uds_proto_get_str(
			payload, len, &pos, &req.content_filter) != 0 ||
	    uds_proto_get_str(
			payload, len, &pos, &req.fields_expr) != 0 ||
	    uds_proto_get_str(
			payload, len, &pos, &req.with_defaults) != 0 ||
	    uds_proto_get_str(
			payload, len, &pos, &req.username) != 0 ||
	    /* RFC 8040 Sec 3.4.1: If-Match */
	    uds_proto_get_str(
			payload, len, &pos, &req.if_match) != 0) {
		send_ipc_response(
			bev, IPC_MSG_DATA_RES, msg_id, 400, NULL, 0);
		goto cleanup;
	}

	req.datastore = (rc_datastore_t)ds;
	req.depth = (int)depth;
	req.with_origin = with_origin ? true : false;
	req.accept_type = (media_type_t)accept;

	dctx = malloc(sizeof(*dctx));
	if (!dctx) {
		send_ipc_response(
			bev, IPC_MSG_DATA_RES, msg_id, 500, NULL, 0);
		goto cleanup;
	}
	dctx->bev = bev;
	dctx->msg_id = msg_id;

	plugin_handle_get(pctx->sr_ctx, &req, on_data_response, dctx);

cleanup:
	free(req.xpath);
	free(req.content_filter);
	free(req.fields_expr);
	free(req.with_defaults);
	free(req.username);
	free(req.if_match);
}

static void handle_edit_req(
	ext_plugin_ctx_t *pctx, struct bufferevent *bev,
	uint32_t msg_id, const uint8_t *payload, size_t len)
{
	size_t pos = 0;
	rc_request_t req = { 0 };
	uint32_t ds = 0, accept = 0, req_type = 0;
	const uint8_t *body_ref = NULL;
	uint32_t body_len = 0;
	uint8_t *body_copy = NULL;
	dispatch_ctx_t *dctx;

	if (uds_proto_get_u32(payload, len, &pos, &ds) != 0 ||
	    uds_proto_get_u32(payload, len, &pos, &accept) != 0 ||
	    uds_proto_get_u32(payload, len, &pos, &req_type) != 0 ||
	    uds_proto_get_str(payload, len, &pos, &req.xpath) != 0 ||
	    uds_proto_get_str(
			payload, len, &pos, (char **)&req.method) != 0 ||
	    uds_proto_get_str(
			payload, len, &pos, &req.username) != 0 ||
	    /* RFC 8040 Sec 3.4.1: If-Match */
	    uds_proto_get_str(
			payload, len, &pos, (char **)&req.if_match) != 0 ||
	    uds_proto_get_bytes_ref(
			payload, len, &pos, &body_ref, &body_len) != 0) {
		send_ipc_response(
			bev, IPC_MSG_EDIT_RES, msg_id, 400, NULL, 0);
		goto cleanup;
	}

	req.datastore = (rc_datastore_t)ds;
	req.accept_type = (media_type_t)accept;
	req.req_type = (media_type_t)req_type;

	if (body_len > 0) {
		body_copy = malloc(body_len);
		if (body_copy)
			memcpy(body_copy, body_ref, body_len);
	}

	dctx = malloc(sizeof(*dctx));
	if (!dctx) {
		send_ipc_response(
			bev, IPC_MSG_EDIT_RES, msg_id, 500, NULL, 0);
		free(body_copy);
		goto cleanup;
	}
	dctx->bev = bev;
	dctx->msg_id = msg_id;

	plugin_handle_edit(
		pctx->sr_ctx, &req, body_copy, body_len,
		on_edit_response, dctx);

	free(body_copy);

cleanup:
	free(req.xpath);
	free((void *)req.method);
	free(req.username);
	free(req.if_match);
}

static void handle_rpc_req(
	ext_plugin_ctx_t *pctx, struct bufferevent *bev,
	uint32_t msg_id, const uint8_t *payload, size_t len)
{
	size_t pos = 0;
	rc_request_t req = { 0 };
	uint32_t accept = 0, req_type = 0;
	const uint8_t *body_ref = NULL;
	uint32_t body_len = 0;
	uint8_t *body_copy = NULL;
	dispatch_ctx_t *dctx;

	if (uds_proto_get_u32(payload, len, &pos, &accept) != 0 ||
	    uds_proto_get_u32(payload, len, &pos, &req_type) != 0 ||
	    uds_proto_get_str(
			payload, len, &pos, &req.rpc_module) != 0 ||
	    uds_proto_get_str(
			payload, len, &pos, &req.rpc_name) != 0 ||
	    uds_proto_get_str(
			payload, len, &pos, &req.username) != 0 ||
	    uds_proto_get_bytes_ref(
			payload, len, &pos, &body_ref, &body_len) != 0) {
		send_ipc_response(
			bev, IPC_MSG_RPC_RES, msg_id, 400, NULL, 0);
		goto cleanup;
	}

	req.accept_type = (media_type_t)accept;
	req.req_type = (media_type_t)req_type;

	if (body_len > 0) {
		body_copy = malloc(body_len);
		if (body_copy)
			memcpy(body_copy, body_ref, body_len);
	}

	dctx = malloc(sizeof(*dctx));
	if (!dctx) {
		send_ipc_response(
			bev, IPC_MSG_RPC_RES, msg_id, 500, NULL, 0);
		free(body_copy);
		goto cleanup;
	}
	dctx->bev = bev;
	dctx->msg_id = msg_id;

	plugin_handle_rpc(
		pctx->sr_ctx, &req, body_copy, body_len,
		on_rpc_response, dctx);

	free(body_copy);

cleanup:
	free(req.rpc_module);
	free(req.rpc_name);
	free(req.username);
}

/*
 * ROADMAP.md item 6.1 - Mode Externe : diffusion des notifications.
 *
 * Le daemon (ce fichier) est celui qui possede la connexion
 * sysrepo et recoit donc les notifications sysrepo via
 * plugin_subscribe_notifications() (sysrepo_plugin.c, cf.
 * daemon_notif_push_cb() ci-dessous). Il doit ensuite les
 * repousser vers chaque gateway actuellement connecte sur l'UDS,
 * via un nouveau message IPC_MSG_NOTIF_PUSH (deja defini dans
 * uds_common.h mais jusqu'ici jamais emis). gw_conn_add/remove
 * maintiennent la liste des connexions gateway actives.
 */
static void gw_conn_add(
	ext_plugin_ctx_t *pctx, struct bufferevent *bev)
{
	gw_conn_t *conn = calloc(1, sizeof(*conn));
	if (!conn) return;
	conn->bev = bev;
	conn->next = pctx->gw_conns;
	pctx->gw_conns = conn;
}

static void gw_conn_remove(
	ext_plugin_ctx_t *pctx, struct bufferevent *bev)
{
	gw_conn_t **cur = &pctx->gw_conns;

	while (*cur) {
		if ((*cur)->bev == bev) {
			gw_conn_t *found = *cur;
			*cur = found->next;
			free(found);
			return;
		}
		cur = &(*cur)->next;
	}
}

/**
 * @brief plugin_notif_cb : diffuse une notification sysrepo a
 * toutes les gateways actuellement connectees sur l'UDS.
 *
 * Appele depuis le thread libevent du daemon (meme thread que
 * sr_subscription_process_events(), cf. sysrepo_plugin.c) : les
 * ecritures bufferevent_write() ci-dessous sont donc sans risque
 * vis-a-vis de la regle d'or #1 (AGENTS.md).
 */
static void daemon_notif_push_cb(
	const char *module_name, const char *xpath,
	const char *payload, void *user_data)
{
	ext_plugin_ctx_t *pctx = (ext_plugin_ctx_t *)user_data;
	uint8_t stackbuf[1024];
	uint8_t *ipc_payload = stackbuf;
	size_t cap = sizeof(stackbuf);
	size_t pos = 0;
	uint8_t *heapbuf = NULL;
	uint8_t *buf = NULL;
	size_t buf_len = 0;
	gw_conn_t *conn;
	int ok;

	if (!pctx->gw_conns)
		return; /* aucune gateway connectee : rien a faire */

	/* Capacite dynamique si le payload JSON depasse le buffer de
	 * pile (notifications avec beaucoup de champs). */
	cap = 64 + strlen(module_name ? module_name : "") +
		strlen(xpath ? xpath : "") +
		strlen(payload ? payload : "");
	if (cap > sizeof(stackbuf)) {
		heapbuf = malloc(cap);
		if (!heapbuf) return;
		ipc_payload = heapbuf;
	} else {
		cap = sizeof(stackbuf);
	}

	ok = uds_proto_put_str(ipc_payload, cap, &pos, module_name);
	ok |= uds_proto_put_str(ipc_payload, cap, &pos, xpath);
	ok |= uds_proto_put_str(ipc_payload, cap, &pos, payload);
	if (ok != 0) {
		RC_WARN("uds-plugin: notification payload too large, "
			"dropped");
		free(heapbuf);
		return;
	}

	if (ipc_serialize_message(
			IPC_MSG_NOTIF_PUSH, 0, 0,
			ipc_payload, pos, &buf, &buf_len) != 0) {
		RC_ERROR("uds-plugin: failed to serialize "
			"IPC_MSG_NOTIF_PUSH");
		free(heapbuf);
		return;
	}
	free(heapbuf);

	for (conn = pctx->gw_conns; conn; conn = conn->next)
		bufferevent_write(conn->bev, buf, buf_len);
	free(buf);
}

static void gateway_read_cb(struct bufferevent *bev, void *ctx)
{
	ext_plugin_ctx_t *pctx = (ext_plugin_ctx_t *)ctx;
	struct evbuffer *input = bufferevent_get_input(bev);

	for (;;) {
		ipc_msg_header_t hdr;
		size_t avail = evbuffer_get_length(input);
		size_t total;
		uint8_t *payload = NULL;

		if (avail < sizeof(hdr))
			break;

		evbuffer_copyout(input, &hdr, sizeof(hdr));
		if (hdr.magic != IPC_MAGIC_NUMBER) {
			RC_ERROR("uds-plugin: bad magic from gateway, "
				"closing connection");
			bufferevent_free(bev);
			return;
		}

		total = sizeof(hdr) + hdr.payload_len;
		if (avail < total)
			break; /* message incomplet, attendre plus */

		evbuffer_drain(input, sizeof(hdr));

		if (hdr.payload_len > 0) {
			payload = malloc(hdr.payload_len);
			if (!payload) {
				evbuffer_drain(input, hdr.payload_len);
				continue;
			}
			evbuffer_remove(
				input, payload, hdr.payload_len);
		}

		switch (hdr.type) {
		case IPC_MSG_DATA_REQ:
			handle_data_req(
				pctx, bev, hdr.msg_id,
				payload, hdr.payload_len);
			break;
		case IPC_MSG_EDIT_REQ:
			handle_edit_req(
				pctx, bev, hdr.msg_id,
				payload, hdr.payload_len);
			break;
		case IPC_MSG_RPC_REQ:
			handle_rpc_req(
				pctx, bev, hdr.msg_id,
				payload, hdr.payload_len);
			break;
		default:
			RC_WARN("uds-plugin: unknown message type %d",
				hdr.type);
			break;
		}

		free(payload);
	}
}

static void gateway_event_cb(
	struct bufferevent *bev, short events, void *ctx)
{
	ext_plugin_ctx_t *pctx = (ext_plugin_ctx_t *)ctx;

	if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
		/* ROADMAP 6.1 : ne plus tenter de pousser de
		 * notifications vers une connexion fermee. */
		gw_conn_remove(pctx, bev);
		bufferevent_free(bev);
	}
}

static void accept_gateway_cb(
	struct evconnlistener *listener UNUSED,
	evutil_socket_t fd,
	struct sockaddr *address UNUSED,
	int socklen UNUSED,
	void *ctx)
{
	ext_plugin_ctx_t *pctx = (ext_plugin_ctx_t *)ctx;
	struct bufferevent *bev = bufferevent_socket_new(
		pctx->base, fd, BEV_OPT_CLOSE_ON_FREE);

	bufferevent_setcb(
		bev, gateway_read_cb, NULL,
		gateway_event_cb, pctx);
	bufferevent_enable(bev, EV_READ | EV_WRITE);

	/* ROADMAP 6.1 : enregistrer la connexion pour la diffusion
	 * des notifications sysrepo (cf. daemon_notif_push_cb()). */
	gw_conn_add(pctx, bev);
}

int ext_plugin_init_uds(
	struct event_base *base,
	const char *uds_path,
	plugin_ctx_t *sr_ctx)
{
	ext_plugin_ctx_t *ctx = calloc(
		1, sizeof(ext_plugin_ctx_t));
	if (!ctx)
		return -1;
	ctx->base = base;
	ctx->sr_ctx = sr_ctx;

	unlink(uds_path);

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

	/* ROADMAP.md item 6.1 : brancher les notifications sysrepo
	 * cote daemon (decouverte multi-modules, cf.
	 * plugin_subscribe_notifications() dans sysrepo_plugin.c) et
	 * les repousser vers chaque gateway connectee via
	 * daemon_notif_push_cb() ci-dessus. Sans ceci, le mode
	 * Externe n'a jamais recu aucune notification (cf. dette
	 * technique ROADMAP.md avant cette session). */
	plugin_subscribe_notifications(
		sr_ctx, daemon_notif_push_cb, ctx);

	return 0;
}
