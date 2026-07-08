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
#include <event2/buffer.h>
#include <libyang/libyang.h> /* Necessaire pour struct ly_ctx */
#include "plugin_api.h"
#include "ipc/uds_common.h"
#include "ipc/uds_data_proto.h"
#include "logger.h"

/*
 * Mode Externe (IPC UDS) - cote Gateway (restconf-server).
 *
 * Ce fichier serialise les operations GET/EDIT/RPC de rc_request_t
 * vers le daemon restconf-plugin via une socket Unix, et route les
 * reponses IPC (correlees par msg_id) vers le callback approprie.
 * Voir uds_plugin.c pour le cote daemon, et
 * include/ipc/uds_data_proto.h pour le format binaire echange.
 */

typedef enum {
	PENDING_DATA,
	PENDING_EDIT,
	PENDING_RPC
} pending_kind_t;

typedef struct pending_req_s {
	uint32_t msg_id;
	pending_kind_t kind;
	union {
		plugin_data_cb data_cb;
		plugin_edit_cb edit_cb;
		plugin_rpc_cb rpc_cb;
	} cb;
	void *user_data;
	struct pending_req_s *next;
} pending_req_t;

struct plugin_ctx_s {
	struct event_base *base;
	struct bufferevent *bev;
	uint32_t next_msg_id;
	pending_req_t *pending;
};

static pending_req_t *add_pending(
	plugin_ctx_t *ctx, uint32_t msg_id, pending_kind_t kind)
{
	pending_req_t *p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;
	p->msg_id = msg_id;
	p->kind = kind;
	p->next = ctx->pending;
	ctx->pending = p;
	return p;
}

static pending_req_t *take_pending(plugin_ctx_t *ctx, uint32_t msg_id)
{
	pending_req_t **cur = &ctx->pending;

	while (*cur) {
		if ((*cur)->msg_id == msg_id) {
			pending_req_t *found = *cur;
			*cur = found->next;
			return found;
		}
		cur = &(*cur)->next;
	}
	return NULL;
}

static void fail_all_pending(plugin_ctx_t *ctx)
{
	pending_req_t *p = ctx->pending;

	ctx->pending = NULL;
	while (p) {
		pending_req_t *next = p->next;

		switch (p->kind) {
		case PENDING_DATA:
			p->cb.data_cb(500, NULL, 0, p->user_data);
			break;
		case PENDING_EDIT:
			p->cb.edit_cb(500, "operation-failed",
				"Lost connection to plugin daemon",
				p->user_data);
			break;
		case PENDING_RPC:
			p->cb.rpc_cb(500, NULL, 0, p->user_data);
			break;
		}
		free(p);
		p = next;
	}
}

/**
 * @brief Route an IPC response (DATA_RES/EDIT_RES/RPC_RES) to the
 * waiting callback that matches the header's msg_id.
 */
static void dispatch_ipc_response(
	plugin_ctx_t *ctx, const ipc_msg_header_t *hdr,
	const uint8_t *payload, size_t len)
{
	pending_req_t *p = take_pending(ctx, hdr->msg_id);

	if (!p) {
		/* Reponse orpheline (timeout cote appelant deja
		 * traite autrement, ou msg_id inconnu) : ignorer. */
		return;
	}

	switch (hdr->type) {
	case IPC_MSG_DATA_RES: {
		uint8_t *body = NULL;

		if (p->kind != PENDING_DATA)
			break;
		if (len > 0) {
			body = malloc(len);
			if (body)
				memcpy(body, payload, len);
		}
		p->cb.data_cb(
			hdr->status_code, body, len, p->user_data);
		break;
	}
	case IPC_MSG_EDIT_RES: {
		size_t pos = 0;
		char *tag = NULL;
		char *msg = NULL;

		if (p->kind != PENDING_EDIT)
			break;
		if (uds_proto_get_str(payload, len, &pos, &tag) != 0 ||
		    uds_proto_get_str(payload, len, &pos, &msg) != 0) {
			p->cb.edit_cb(500, "operation-failed",
				"Malformed IPC response",
				p->user_data);
		} else {
			p->cb.edit_cb(
				hdr->status_code,
				tag ? tag : "operation-failed",
				msg ? msg : "Unknown error",
				p->user_data);
		}
		free(tag);
		free(msg);
		break;
	}
	case IPC_MSG_RPC_RES: {
		uint8_t *body = NULL;

		if (p->kind != PENDING_RPC)
			break;
		if (len > 0) {
			body = malloc(len);
			if (body)
				memcpy(body, payload, len);
		}
		p->cb.rpc_cb(
			hdr->status_code, body, len, p->user_data);
		break;
	}
	default:
		break;
	}

	free(p);
}

static void uds_read_cb(struct bufferevent *bev, void *ctx_ptr)
{
	plugin_ctx_t *ctx = (plugin_ctx_t *)ctx_ptr;
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
			RC_ERROR("uds: bad magic from plugin daemon, "
				"closing connection");
			bufferevent_free(bev);
			ctx->bev = NULL;
			fail_all_pending(ctx);
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

		dispatch_ipc_response(
			ctx, &hdr, payload, hdr.payload_len);
		free(payload);
	}
}

static void uds_event_cb(
	struct bufferevent *bev, short events, void *ctx_ptr)
{
	plugin_ctx_t *ctx = (plugin_ctx_t *)ctx_ptr;

	if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
		RC_ERROR("uds: connection to plugin daemon lost");
		fail_all_pending(ctx);
		ctx->bev = NULL;
		bufferevent_free(bev);
	}
}

plugin_ctx_t *plugin_init(
	struct event_base *base, bool use_external UNUSED,
	const char *uds_path)
{
	plugin_ctx_t *ctx = calloc(1, sizeof(plugin_ctx_t));
	if (!ctx)
		return NULL;
	ctx->base = base;
	ctx->next_msg_id = 1;

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
	plugin_ctx_t *ctx, const rc_request_t *req,
	plugin_data_cb callback, void *user_data)
{
	size_t cap;
	uint8_t *payload;
	size_t pos = 0;
	int ok = 0;
	uint32_t msg_id;
	uint8_t *buf = NULL;
	size_t buf_len = 0;
	pending_req_t *p;
	int rc;

	if (!ctx->bev) {
		callback(500, NULL, 0, user_data);
		return;
	}

	/* Dynamic capacity: multi-key list xpaths can
	 * easily exceed a fixed-size buffer. */
	cap = 64 +
		(req->xpath ? strlen(req->xpath) : 0) +
		(req->content_filter ? strlen(req->content_filter) : 0) +
		(req->fields_expr ? strlen(req->fields_expr) : 0) +
		(req->with_defaults ? strlen(req->with_defaults) : 0) +
		(req->username ? strlen(req->username) : 0);
	payload = malloc(cap);
	if (!payload) {
		callback(500, NULL, 0, user_data);
		return;
	}

	ok |= uds_proto_put_u32(payload, cap, &pos,
		(uint32_t)req->datastore);
	ok |= uds_proto_put_i32(payload, cap, &pos,
		(int32_t)req->depth);
	ok |= uds_proto_put_u8(payload, cap, &pos,
		req->with_origin ? 1 : 0);
	ok |= uds_proto_put_u32(payload, cap, &pos,
		(uint32_t)req->accept_type);
	ok |= uds_proto_put_str(payload, cap, &pos,
		req->xpath);
	ok |= uds_proto_put_str(payload, cap, &pos,
		req->content_filter);
	ok |= uds_proto_put_str(payload, cap, &pos,
		req->fields_expr);
	ok |= uds_proto_put_str(payload, cap, &pos,
		req->with_defaults);
	ok |= uds_proto_put_str(payload, cap, &pos,
		req->username);

	if (ok != 0) {
		free(payload);
		callback(500, NULL, 0, user_data);
		return;
	}

	msg_id = ctx->next_msg_id++;
	rc = ipc_serialize_message(
		IPC_MSG_DATA_REQ, msg_id, 0,
		payload, pos, &buf, &buf_len);
	free(payload);

	if (rc != 0) {
		callback(500, NULL, 0, user_data);
		return;
	}

	p = add_pending(ctx, msg_id, PENDING_DATA);
	if (!p) {
		free(buf);
		callback(500, NULL, 0, user_data);
		return;
	}
	p->cb.data_cb = callback;
	p->user_data = user_data;

	bufferevent_write(ctx->bev, buf, buf_len);
	free(buf);
}

void plugin_handle_edit(
	plugin_ctx_t *ctx, const rc_request_t *req,
	const uint8_t *body, size_t body_len,
	plugin_edit_cb callback, void *user_data)
{
	size_t cap = 4096 + body_len;
	uint8_t *payload;
	size_t pos = 0;
	int ok = 0;
	uint32_t msg_id;
	uint8_t *buf = NULL;
	size_t buf_len = 0;
	pending_req_t *p;
	int rc;

	if (!ctx->bev) {
		callback(500, "operation-failed",
			"Lost connection to plugin daemon",
			user_data);
		return;
	}

	payload = malloc(cap);
	if (!payload) {
		callback(500, "operation-failed",
			"Out of memory", user_data);
		return;
	}

	ok |= uds_proto_put_u32(payload, cap, &pos,
		(uint32_t)req->datastore);
	ok |= uds_proto_put_u32(payload, cap, &pos,
		(uint32_t)req->accept_type);
	ok |= uds_proto_put_u32(payload, cap, &pos,
		(uint32_t)req->req_type);
	ok |= uds_proto_put_str(payload, cap, &pos, req->xpath);
	ok |= uds_proto_put_str(payload, cap, &pos, req->method);
	ok |= uds_proto_put_str(payload, cap, &pos, req->username);
	ok |= uds_proto_put_bytes(payload, cap, &pos,
		body, (uint32_t)body_len);

	if (ok != 0) {
		free(payload);
		callback(500, "operation-failed",
			"Request too large", user_data);
		return;
	}

	msg_id = ctx->next_msg_id++;
	rc = ipc_serialize_message(
		IPC_MSG_EDIT_REQ, msg_id, 0,
		payload, pos, &buf, &buf_len);
	free(payload);

	if (rc != 0) {
		callback(500, "operation-failed",
			"Serialization failed", user_data);
		return;
	}

	p = add_pending(ctx, msg_id, PENDING_EDIT);
	if (!p) {
		free(buf);
		callback(500, "operation-failed",
			"Out of memory", user_data);
		return;
	}
	p->cb.edit_cb = callback;
	p->user_data = user_data;

	bufferevent_write(ctx->bev, buf, buf_len);
	free(buf);
}

void plugin_handle_rpc(
	plugin_ctx_t *ctx,
	const rc_request_t *req,
	const uint8_t *body,
	size_t body_len,
	plugin_rpc_cb callback,
	void *user_data)
{
	size_t cap = 4096 + body_len;
	uint8_t *payload;
	size_t pos = 0;
	int ok = 0;
	uint32_t msg_id;
	uint8_t *buf = NULL;
	size_t buf_len = 0;
	pending_req_t *p;
	int rc;

	if (!ctx->bev) {
		callback(500, NULL, 0, user_data);
		return;
	}

	payload = malloc(cap);
	if (!payload) {
		callback(500, NULL, 0, user_data);
		return;
	}

	ok |= uds_proto_put_u32(payload, cap, &pos,
		(uint32_t)req->accept_type);
	ok |= uds_proto_put_u32(payload, cap, &pos,
		(uint32_t)req->req_type);
	ok |= uds_proto_put_str(payload, cap, &pos, req->rpc_module);
	ok |= uds_proto_put_str(payload, cap, &pos, req->rpc_name);
	ok |= uds_proto_put_str(payload, cap, &pos, req->username);
	ok |= uds_proto_put_bytes(payload, cap, &pos,
		body, (uint32_t)body_len);

	if (ok != 0) {
		free(payload);
		callback(500, NULL, 0, user_data);
		return;
	}

	msg_id = ctx->next_msg_id++;
	rc = ipc_serialize_message(
		IPC_MSG_RPC_REQ, msg_id, 0,
		payload, pos, &buf, &buf_len);
	free(payload);

	if (rc != 0) {
		callback(500, NULL, 0, user_data);
		return;
	}

	p = add_pending(ctx, msg_id, PENDING_RPC);
	if (!p) {
		free(buf);
		callback(500, NULL, 0, user_data);
		return;
	}
	p->cb.rpc_cb = callback;
	p->user_data = user_data;

	bufferevent_write(ctx->bev, buf, buf_len);
	free(buf);
}

void plugin_subscribe_notifications(
	plugin_ctx_t *ctx UNUSED,
	plugin_notif_cb callback UNUSED,
	void *user_data UNUSED)
{
	/* TODO: Envoyer une requete d'abonnement via UDS et router
	 * les IPC_MSG_NOTIF_PUSH recus vers callback(). Depend de
	 * l'implementation complete du RPC establish-subscription
	 * (voir rpc_establish_sub_cb dans sysrepo_plugin.c). */
}

void plugin_destroy(plugin_ctx_t *ctx)
{
	if (!ctx) return;
	fail_all_pending(ctx);
	if (ctx->bev) bufferevent_free(ctx->bev);
	free(ctx);
}

/* ============================================================
 * Gestion du contexte libyang pour le mode Externe
 * ============================================================ */

const struct ly_ctx *plugin_acquire_ly_ctx(plugin_ctx_t *ctx)
{
	/* En mode externe, le gateway n'a pas d'acces direct a sysrepo.
	 * On retourne NULL. Le routeur devra fonctionner sans contexte
	 * (la resolution des cles de listes sera desactivee).
	 * TODO (roadmap 3.10): Implementer un ly_ctx local dans le
	 * gateway (charge les memes modules YANG que sysrepo) ou le
	 * recuperer via un nouvel echange IPC dedie. */
	(void)ctx;
	return NULL;
}

void plugin_release_ly_ctx(plugin_ctx_t *ctx)
{
	/* Rien a liberer puisque nous n'avons pas acquis de contexte */
	(void)ctx;
}
