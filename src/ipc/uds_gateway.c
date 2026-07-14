#define _GNU_SOURCE
#define UNUSED __attribute__((unused))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
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

/* ROADMAP.md item 3.10: default base path of the sysrepo YANG
 * module repository (modules live under <path>/yang). Overridable
 * at compile time via -DSR_REPO_PATH_DEFAULT and at runtime via
 * the SYSREPO_REPOSITORY_PATH environment variable, matching
 * sysrepo's own convention. */
#ifndef SR_REPO_PATH_DEFAULT
#define SR_REPO_PATH_DEFAULT "/etc/sysrepo"
#endif

/* RFC 8040 Sec 3.4.1: ETag computation (same as sysrepo_plugin.c) */
static uint32_t gw_fnv1a_hash(const uint8_t *data, size_t len)
{
	uint32_t h = 0x811c9dc5u;
	for (size_t i = 0; i < len; i++) {
		h ^= (uint32_t)data[i];
		h *= 0x01000193u;
	}
	return h;
}

static char *gw_compute_etag(const uint8_t *body, size_t len)
{
	char *etag;

	if (!body || len == 0) return NULL;
	if (asprintf(&etag, "\"%08x\"",
		gw_fnv1a_hash(body, len)) < 0)
		return NULL;
	return etag;
}

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
	/* ROADMAP 3.10: local schema-only libyang context, built once
	 * at plugin_init() time from the sysrepo YANG repository on
	 * disk. Read-only after init, never mutated concurrently
	 * (gateway process is single-threaded, AGENTS.md rule #1). */
	struct ly_ctx *local_ly_ctx;
	/* ROADMAP 6.1: notification callback set by
	 * plugin_subscribe_notifications(), invoked from
	 * uds_read_cb() on IPC_MSG_NOTIF_PUSH (cf. uds_plugin.c
	 * daemon_notif_push_cb()). */
	plugin_notif_cb notif_cb;
	void *notif_user_data;
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
			p->cb.data_cb(500, NULL, 0, NULL,
				p->user_data);
			break;
		case PENDING_EDIT:
			p->cb.edit_cb(500, "operation-failed",
				"Lost connection to plugin daemon",
				NULL, p->user_data);
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
 * waiting callback that matches the header's msg_id, or forward an
 * unsolicited IPC_MSG_NOTIF_PUSH (ROADMAP 6.1) to the registered
 * notification callback.
 */
static void dispatch_ipc_response(
	plugin_ctx_t *ctx, const ipc_msg_header_t *hdr,
	const uint8_t *payload, size_t len)
{
	pending_req_t *p;

	if (hdr->type == IPC_MSG_NOTIF_PUSH) {
		/* ROADMAP.md item 6.1 : notification poussee par le
		 * daemon (uds_plugin.c: daemon_notif_push_cb()), non
		 * correlee a un msg_id de requete (0, cf. emission). */
		size_t pos = 0;
		char *module_name = NULL;
		char *xpath = NULL;
		char *notif_payload = NULL;

		if (uds_proto_get_str(
				payload, len, &pos, &module_name) == 0 &&
		    uds_proto_get_str(
				payload, len, &pos, &xpath) == 0 &&
		    uds_proto_get_str(
				payload, len, &pos, &notif_payload) == 0 &&
		    ctx->notif_cb) {
			/* Mode Externe : aucun noeud lyd_node disponible
			 * (cf. plugin_notif_cb dans plugin_api.h) -- un
			 * filtre XPath par-souscription (ROADMAP.md item
			 * 6.1 suivi) ne peut donc pas etre evalue ici et
			 * est ignore par le fan-out SSE (main.c), qui livre
			 * alors la notification non filtree. */
			ctx->notif_cb(
				module_name ? module_name : "",
				xpath ? xpath : "",
				notif_payload ? notif_payload : "",
				NULL,
				ctx->notif_user_data);
		}
		free(module_name);
		free(xpath);
		free(notif_payload);
		return;
	}

	p = take_pending(ctx, hdr->msg_id);

	if (!p) {
		/* Reponse orpheline (timeout cote appelant deja
		 * traite autrement, ou msg_id inconnu) : ignorer. */
		return;
	}

	switch (hdr->type) {
	case IPC_MSG_DATA_RES: {
		uint8_t *body = NULL;
		char *etag = NULL;

		if (p->kind != PENDING_DATA)
			break;
		if (len > 0) {
			body = malloc(len);
			if (body)
				memcpy(body, payload, len);
			/* RFC 8040 Sec 3.4.1: compute ETag from body */
			etag = gw_compute_etag(payload, len);
		}
		p->cb.data_cb(
			hdr->status_code, body, len, etag,
			p->user_data);
		free(etag);
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
				NULL, p->user_data);
		} else {
			p->cb.edit_cb(
			hdr->status_code,
			tag ? tag : "operation-failed",
			msg ? msg : "Unknown error",
			NULL, /* etag — not yet returned by plugin */
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

/*
 * ROADMAP.md item 3.10 - Contexte libyang local (mode Externe).
 *
 * En mode Externe, le process gateway n'a pas de connexion sysrepo
 * directe : plugin_acquire_ly_ctx() renvoyait NULL, desactivant la
 * resolution des cles de liste dans le routeur (router.c). Plutot
 * que d'ajouter un aller-retour IPC bloquant sur le chemin critique
 * de chaque requete (ce qui reintroduirait un risque de blocage du
 * thread libevent si le daemon est lent/indisponible, cf. AGENTS.md
 * regle d'or #2), on construit ici un contexte libyang *local et en
 * lecture seule*, charge une seule fois au demarrage depuis le meme
 * repertoire de stockage YANG que sysrepo (celui peuple par
 * `sysrepoctl -i`, cf. docker/Dockerfile).
 *
 * Ce contexte ne sert qu'a la resolution de schema (noms de
 * modules, cles de liste) dans router.c : il ne touche jamais aux
 * donnees et n'a donc pas besoin d'etre synchronise en direct avec
 * sysrepo.
 *
 * @warning Limite connue : si un module est installe ou desinstalle
 * a chaud dans sysrepo, le gateway doit etre redemarre pour que la
 * resolution de schema en tienne compte (evenement rare en
 * pratique, cf. dette technique ROADMAP.md).
 *
 * @return Nouveau contexte libyang, ou NULL si le repertoire YANG
 *         est inaccessible ou la creation du contexte echoue (la
 *         resolution de cles de liste reste alors desactivee,
 *         comme avant cette evolution).
 */
static struct ly_ctx *build_local_ly_ctx(void)
{
	const char *repo_path = getenv("SYSREPO_REPOSITORY_PATH");
	char yang_dir[512];
	struct ly_ctx *ctx = NULL;
	DIR *dir;
	struct dirent *ent;
	unsigned int loaded = 0, failed = 0;

	if (!repo_path || !*repo_path)
		repo_path = SR_REPO_PATH_DEFAULT;
	snprintf(yang_dir, sizeof(yang_dir), "%s/yang", repo_path);

	dir = opendir(yang_dir);
	if (!dir) {
		RC_WARN("uds-gateway: cannot open YANG dir '%s' (%s); "
			"list key resolution disabled (ROADMAP 3.10)",
			yang_dir, strerror(errno));
		return NULL;
	}

	if (ly_ctx_new(yang_dir, 0, &ctx) != LY_SUCCESS || !ctx) {
		RC_WARN("uds-gateway: ly_ctx_new() failed for '%s'",
			yang_dir);
		closedir(dir);
		return NULL;
	}

	while ((ent = readdir(dir)) != NULL) {
		size_t name_len = strlen(ent->d_name);
		char full_path[1024];

		if (name_len < 6 ||
		    strcmp(ent->d_name + name_len - 5, ".yang") != 0)
			continue;

		snprintf(full_path, sizeof(full_path), "%s/%s",
			yang_dir, ent->d_name);

		/* Best-effort: une erreur sur un module isole (import
		 * manquant, revision en doublon deja implementee,
		 * etc.) ne doit pas empecher le chargement des autres :
		 * la resolution degrade proprement au cas par cas dans
		 * router.c (cf. resolve_module_name()). */
		if (lys_parse_path(
				ctx, full_path, LYS_IN_YANG,
				NULL) != LY_SUCCESS) {
			RC_DEBUG("uds-gateway: failed to parse '%s' "
				"into local ly_ctx (non-fatal)",
				full_path);
			failed++;
		} else {
			loaded++;
		}
	}
	closedir(dir);

	RC_INFO("uds-gateway: local libyang context built from '%s' "
		"(%u modules loaded, %u skipped) [ROADMAP 3.10]",
		yang_dir, loaded, failed);
	return ctx;
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
		RC_ERROR("Cannot connect to %s", uds_path);
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

	/* ROADMAP 3.10: local schema-only context, independent of
	 * the UDS connection above (best-effort; NULL is a valid,
	 * already-handled outcome for callers of
	 * plugin_acquire_ly_ctx()). */
	ctx->local_ly_ctx = build_local_ly_ctx();

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
		callback(500, NULL, 0, NULL, user_data);
		return;
	}

	/* Dynamic capacity: multi-key list xpaths can
	 * easily exceed a fixed-size buffer. */
	cap = 64 +
		(req->xpath ? strlen(req->xpath) : 0) +
		(req->content_filter ? strlen(req->content_filter) : 0) +
		(req->fields_expr ? strlen(req->fields_expr) : 0) +
		(req->with_defaults ? strlen(req->with_defaults) : 0) +
		(req->username ? strlen(req->username) : 0) +
		(req->if_match ? strlen(req->if_match) : 0);
	payload = malloc(cap);
	if (!payload) {
		callback(500, NULL, 0, NULL, user_data);
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
	/* RFC 8040 Sec 3.4.1: If-Match header */
	ok |= uds_proto_put_str(payload, cap, &pos,
		req->if_match);

	if (ok != 0) {
		free(payload);
		callback(500, NULL, 0, NULL, user_data);
		return;
	}

	msg_id = ctx->next_msg_id++;
	rc = ipc_serialize_message(
		IPC_MSG_DATA_REQ, msg_id, 0,
		payload, pos, &buf, &buf_len);
	free(payload);

	if (rc != 0) {
		callback(500, NULL, 0, NULL, user_data);
		return;
	}

	p = add_pending(ctx, msg_id, PENDING_DATA);
	if (!p) {
		free(buf);
		callback(500, NULL, 0, NULL, user_data);
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
			NULL, user_data);
		return;
	}

	payload = malloc(cap);
	if (!payload) {
		callback(500, "operation-failed",
			"Out of memory", NULL, user_data);
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
	/* RFC 8040 Sec 3.4.1: If-Match */
	ok |= uds_proto_put_str(payload, cap, &pos, req->if_match);
	ok |= uds_proto_put_bytes(payload, cap, &pos,
		body, (uint32_t)body_len);

	if (ok != 0) {
		free(payload);
		callback(500, "operation-failed",
			"Request too large", NULL, user_data);
		return;
	}

	msg_id = ctx->next_msg_id++;
	rc = ipc_serialize_message(
		IPC_MSG_EDIT_REQ, msg_id, 0,
		payload, pos, &buf, &buf_len);
	free(payload);

	if (rc != 0) {
		callback(500, "operation-failed",
			"Serialization failed", NULL, user_data);
		return;
	}

	p = add_pending(ctx, msg_id, PENDING_EDIT);
	if (!p) {
		free(buf);
		callback(500, "operation-failed",
			"Out of memory", NULL, user_data);
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
	plugin_ctx_t *ctx, plugin_notif_cb callback,
	void *user_data)
{
	/*
	 * ROADMAP.md item 6.1 - Mode Externe.
	 *
	 * Cote gateway, il n'y a rien a "souscrire" explicitement :
	 * c'est le daemon (uds_plugin.c) qui possede la connexion
	 * sysrepo et decide, via son propre appel a
	 * plugin_subscribe_notifications() (sysrepo_plugin.c, cote
	 * interne, declenche depuis ext_plugin_init_uds()), a quels
	 * modules il s'abonne. Le gateway se contente d'enregistrer
	 * le callback applicatif (cf. on_notification_cb() dans
	 * main.c) : il sera invoque par dispatch_ipc_response() a
	 * chaque IPC_MSG_NOTIF_PUSH recu du daemon, pour n'importe
	 * quelle connexion UDS active au moment de l'envoi.
	 */
	if (!ctx) return;
	ctx->notif_cb = callback;
	ctx->notif_user_data = user_data;
}

plugin_replay_sub_t *plugin_open_replay_subscription(
	plugin_ctx_t *ctx UNUSED, time_t start_time UNUSED,
	time_t stop_time UNUSED, plugin_notif_cb callback UNUSED,
	void *user_data UNUSED)
{
	/*
	 * ROADMAP.md item 6.5 : le replay de notifications n'est pas
	 * encore supporte en mode Externe. Le rendre reel demanderait
	 * un nouveau message IPC dedie (ex. IPC_MSG_REPLAY_OPEN) que
	 * le daemon traduirait en une souscription sysrepo privee
	 * (cf. plugin_open_replay_subscription() dans
	 * sysrepo_plugin.c pour l'equivalent cote interne), plus un
	 * routage par flux plutot que par simple broadcast
	 * IPC_MSG_NOTIF_PUSH. Renvoyer NULL ici est un echec "doux" :
	 * l'appelant (main.c) retombe alors sur un flux live-only,
	 * sans erreur bloquante pour le client.
	 */
	RC_WARN("uds-gateway: notification replay (start-time) is "
		"not supported in External Plugin mode yet "
		"(ROADMAP 6.5); falling back to a live-only stream");
	return NULL;
}

void plugin_close_replay_subscription(
	plugin_ctx_t *ctx UNUSED, plugin_replay_sub_t *handle UNUSED)
{
	/* Rien a liberer : plugin_open_replay_subscription() renvoie
	 * toujours NULL en mode Externe, donc handle est toujours
	 * NULL ici en pratique. */
}

void plugin_destroy(plugin_ctx_t *ctx)
{
	if (!ctx) return;
	fail_all_pending(ctx);
	if (ctx->bev) bufferevent_free(ctx->bev);
	/* ROADMAP 3.10: liberer le contexte local s'il a ete
	 * construit avec succes dans plugin_init(). */
	if (ctx->local_ly_ctx) ly_ctx_destroy(ctx->local_ly_ctx);
	free(ctx);
}

/* ============================================================
 * Gestion du contexte libyang pour le mode Externe
 * ============================================================ */

const struct ly_ctx *plugin_acquire_ly_ctx(plugin_ctx_t *ctx)
{
	/* ROADMAP 3.10: le contexte local est construit une seule
	 * fois dans plugin_init() (voir build_local_ly_ctx()) et
	 * jamais mute ensuite : aucun verrou n'est necessaire ici,
	 * le process gateway etant strictement mono-thread
	 * (AGENTS.md regle d'or #1). Peut rester NULL si le
	 * repertoire YANG de sysrepo etait inaccessible au demarrage
	 * (voir logs) ; les appelants (router.c) gerent deja ce cas. */
	if (!ctx)
		return NULL;
	return ctx->local_ly_ctx;
}

void plugin_release_ly_ctx(plugin_ctx_t *ctx)
{
	/* Rien a liberer : le contexte local (s'il existe) vit
	 * jusqu'a plugin_destroy(), il n'est pas acquis/libere par
	 * requete comme cote sysrepo (cf. sr_worker_acquire_ly_ctx). */
	(void)ctx;
}
