/**
 * @file sysrepo_worker.c
 * @brief Confined sysrepo worker thread (ROADMAP 3.12).
 *
 * All blocking sysrepo calls (sr_get_data, sr_apply_changes,
 * sr_rpc_send_tree) are isolated in this single pthread.
 * The libevent thread never touches sysrepo directly (except
 * sr_acquire_context/sr_release_context which sysrepo documents
 * as connection-level thread-safe).
 *
 * Subscriptions (oper_get, rpc_subscribe_tree, notif_subscribe)
 * are handled by the plugin (sysrepo_plugin.c) in the libevent
 * thread with SR_SUBSCR_NO_THREAD.  This worker only creates
 * short-lived sessions on the shared connection for GET/EDIT/RPC.
 */
#define _GNU_SOURCE
#define UNUSED __attribute__((unused))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <time.h>
#include <errno.h>
#include <sysrepo.h>
#include <libyang/libyang.h>
#include <event2/event.h>
#include <fcntl.h>
#include "sysrepo_worker.h"
#include "codec.h"
#include "logger.h"


/* ====================================================================
 * Internal message types
 * ==================================================================== */

typedef enum {
	SR_WORKER_GET,
	SR_WORKER_EDIT,
	SR_WORKER_RPC,
	SR_WORKER_SHUTDOWN,
} sr_worker_op_t;

typedef struct sr_worker_msg_s {
	sr_worker_op_t op;
	uint64_t id;
	struct sr_worker_msg_s *next;

	/* Continuation callback + user data */
	union {
		plugin_data_cb data_cb;
		plugin_edit_cb edit_cb;
		plugin_rpc_cb rpc_cb;
	} cb;
	void *user_data;

	/* Deep-copied request fields */
	rc_resource_type_t res_type;
	rc_datastore_t datastore;
	char *username;
	char *xpath;
	char *content_filter;
	int depth;
	char *fields_expr;
	char *with_defaults;
	bool with_origin;
	media_type_t accept_type;
	media_type_t req_type;
	char *method;
	char *if_match;
	char *rpc_module;
	char *rpc_name;

	/* Body (deep copy for EDIT/RPC) */
	uint8_t *body;
	size_t body_len;
} sr_worker_msg_t;

/* ====================================================================
 * Completion queue (worker -> libevent)
 * ==================================================================== */

typedef enum {
	SR_COMP_DATA,
	SR_COMP_EDIT,
	SR_COMP_RPC,
} sr_comp_type_t;

typedef struct sr_completion_s {
	sr_comp_type_t type;
	struct sr_completion_s *next;

	union {
		struct {
			plugin_data_cb cb;
			void *user_data;
			int status;
			uint8_t *body;
			size_t body_len;
			char *etag;
		} data;
		struct {
			plugin_edit_cb cb;
			void *user_data;
			int status;
			char *error_tag;
			char *error_msg;
		} edit;
		struct {
			plugin_rpc_cb cb;
			void *user_data;
			int status;
			uint8_t *body;
			size_t body_len;
		} rpc;
	};
} sr_completion_t;

/* ====================================================================
 * Worker structure
 * ==================================================================== */

struct sr_worker_s {
	pthread_t thread;
	volatile bool running;

	/* Request queue: libevent -> worker */
	pthread_mutex_t req_mutex;
	pthread_cond_t req_cond;
	sr_worker_msg_t *req_head;
	sr_worker_msg_t *req_tail;

	/* Completion queue: worker -> libevent */
	pthread_mutex_t comp_mutex;
	sr_completion_t *comp_head;
	sr_completion_t *comp_tail;

	/* Wakeup pipe: libevent writes [1], worker reads [0] */
	int wakeup_pipe[2];

	/* Completion eventfd: worker writes, libevent reads */
	int comp_fd;

	/* libevent integration */
	struct event_base *base;
	struct event *comp_event;

	/* Shared sysrepo connection (owned by plugin) */
	sr_conn_ctx_t *conn;

	/* Message ID counter */
	uint64_t next_id;
};

/* ====================================================================
 * Helper: safe strdup
 * ==================================================================== */

static char *safe_strdup(const char *s)
{
	return s ? strdup(s) : NULL;
}

/* ====================================================================
 * Helper: free a message and all its deep copies
 * ==================================================================== */

static void free_msg(sr_worker_msg_t *msg)
{
	if (!msg) return;
	free(msg->username);
	free(msg->xpath);
	free(msg->content_filter);
	free(msg->fields_expr);
	free(msg->with_defaults);
	free(msg->method);
	free(msg->if_match);
	free(msg->rpc_module);
	free(msg->rpc_name);
	free(msg->body);
	free(msg);
}

/* ====================================================================
 * Helper: free a completion
 * ==================================================================== */

static void free_completion(sr_completion_t *c)
{
	if (!c) return;
	switch (c->type) {
	case SR_COMP_DATA:
		free(c->data.body);
		free(c->data.etag);
		break;
	case SR_COMP_EDIT:
		free(c->edit.error_tag);
		free(c->edit.error_msg);
		break;
	case SR_COMP_RPC:
		free(c->rpc.body);
		break;
	}
	free(c);
}

/* ====================================================================
 * Queue management
 * ==================================================================== */

static void enqueue_request(
	sr_worker_t *w, sr_worker_msg_t *msg)
{
	RC_TRACE("Try enqueue request...");

	msg->next = NULL;
	pthread_mutex_lock(&w->req_mutex);
	if (w->req_tail)
		w->req_tail->next = msg;
	else
		w->req_head = msg;
	w->req_tail = msg;
	pthread_cond_signal(&w->req_cond);
	pthread_mutex_unlock(&w->req_mutex);

	RC_TRACE("Enqueue request");
	/* Wake the worker via pipe (non-blocking) */
	char byte = 1;
	if (write(w->wakeup_pipe[1], &byte, 1) < 0)
		RC_WARN("worker wakeup write failed: %s",
		        strerror(errno));
}

static sr_worker_msg_t *dequeue_request(sr_worker_t *w)
{
	sr_worker_msg_t *msg;

	RC_TRACE("Try dequeue request...");
	pthread_mutex_lock(&w->req_mutex);
	msg = w->req_head;
	if (msg) {
		w->req_head = msg->next;
		if (!w->req_head)
			w->req_tail = NULL;
	}
	pthread_mutex_unlock(&w->req_mutex);
	RC_TRACE("Dequeue request");
	return msg;
}

static void enqueue_completion(
	sr_worker_t *w, sr_completion_t *c)
{
	c->next = NULL;
	pthread_mutex_lock(&w->comp_mutex);
	if (w->comp_tail)
		w->comp_tail->next = c;
	else
		w->comp_head = c;
	w->comp_tail = c;
	pthread_mutex_unlock(&w->comp_mutex);

	/* Signal libevent via eventfd */
	uint64_t val = 1;
	if (write(w->comp_fd, &val, sizeof(val)) < 0)
		RC_WARN("worker comp_fd write failed: %s",
		        strerror(errno));
}

/* ====================================================================
 * FNV-1a hash (same as sysrepo_plugin.c for ETag)
 * ==================================================================== */

static uint32_t worker_fnv1a(const uint8_t *data, size_t len)
{
	uint32_t h = 0x811c9dc5u;
	for (size_t i = 0; i < len; i++) {
		h ^= (uint32_t)data[i];
		h *= 0x01000193u;
	}
	return h;
}

static char *worker_compute_etag(
	const uint8_t *body, size_t len)
{
	char *etag;

	if (!body || len == 0) return NULL;
	if (asprintf(&etag, "\"%08x\"",
	             worker_fnv1a(body, len)) < 0)
		return NULL;
	return etag;
}

/* ====================================================================
 * Datastore mapping
 * ==================================================================== */

static sr_datastore_t map_datastore(rc_datastore_t ds)
{
	switch (ds) {
	case RC_DS_OPERATIONAL:
	case RC_DS_INTENDED:
		return SR_DS_OPERATIONAL;
	case RC_DS_CANDIDATE:
		return SR_DS_CANDIDATE;
	case RC_DS_STARTUP:
		return SR_DS_STARTUP;
	case RC_DS_RUNNING:
	default:
		return SR_DS_RUNNING;
	}
}

/* ====================================================================
 * Open/close per-request session
 * ==================================================================== */

static int worker_open_session(
	sr_worker_t *w, rc_datastore_t ds,
	const char *username, sr_session_ctx_t **out)
{
	int rc;

	rc = sr_session_start(
		w->conn, map_datastore(ds), out);
	if (rc != SR_ERR_OK) {
		*out = NULL;
		return rc;
	}
	if (username && *username)
		sr_session_set_user(*out, username);
	return SR_ERR_OK;
}

static void worker_close_session(sr_session_ctx_t *sess)
{
	if (sess)
		sr_session_stop(sess);
}

/* ====================================================================
 * Recursive leaf setter
 * ==================================================================== */

static int worker_set_leaves_recursive(
	sr_session_ctx_t *sess,
	struct lyd_node *node,
	const char *default_op UNUSED,
	int *set_count)
{
	if (!node || !node->schema) return SR_ERR_OK;

	if (node->schema->nodetype == LYS_LEAF ||
	    node->schema->nodetype == LYS_LEAFLIST) {
		const char *value = lyd_get_value(node);
		char *leaf_xpath = lyd_path(
			node, LYD_PATH_STD, NULL, 0);

		if (leaf_xpath) {
			uint32_t opts = SR_EDIT_ISOLATE;
			int set_rc = sr_set_item_str(
				sess, leaf_xpath,
				value, NULL, opts);
			free(leaf_xpath);
			if (set_rc != SR_ERR_OK)
				return set_rc;
			(*set_count)++;
		}
	}

	struct lyd_node *child = lyd_child(node);
	while (child) {
		int rc = worker_set_leaves_recursive(
			sess, child, default_op,
			set_count);
		if (rc != SR_ERR_OK) return rc;
		child = child->next;
	}
	return SR_ERR_OK;
}

/* ====================================================================
 * Process individual message types
 * ==================================================================== */

/**
 * @brief Process a GET request in the worker thread.
 */
static void process_get(
	sr_worker_t *w, sr_worker_msg_t *msg)
{
	sr_completion_t *c = calloc(1, sizeof(*c));
	if (!c) return;
	c->type = SR_COMP_DATA;
	c->data.cb = msg->cb.data_cb;
	c->data.user_data = msg->user_data;

	/* Unknown datastore -> 400 */
	if (msg->datastore == RC_DS_UNKNOWN) {
		const char *tag = "invalid-value";
		const char *m = "Unknown or unsupported "
		                "datastore";
		codec_serialize_errors(
			msg->accept_type, tag, m,
			(char **)&c->data.body,
			&c->data.body_len);
		c->data.status = 400;
		enqueue_completion(w, c);
		return;
	}

	/* Determine session datastore */
	rc_datastore_t sess_ds;
	if (msg->res_type == RC_RES_DATA)
		sess_ds = RC_DS_OPERATIONAL;
	else
		sess_ds = msg->datastore;

	sr_session_ctx_t *sess = NULL;
	int open_rc = worker_open_session(
		w, sess_ds, msg->username, &sess);
	if (open_rc != SR_ERR_OK) {
		codec_serialize_errors(
			msg->accept_type,
			"operation-failed",
			sr_strerror(open_rc),
			(char **)&c->data.body,
			&c->data.body_len);
		c->data.status = 500;
		enqueue_completion(w, c);
		return;
	}

	/* Build sr_get_data options */
	sr_get_oper_flag_t opts = 0;
	if (msg->content_filter) {
		if (strcmp(msg->content_filter,
		           "config") == 0)
			opts |= SR_OPER_NO_STATE;
		else if (strcmp(msg->content_filter,
		                "nonconfig") == 0)
			opts |= SR_OPER_NO_CONFIG;
	}
	if (msg->with_origin &&
	    msg->datastore == RC_DS_OPERATIONAL)
		opts |= SR_OPER_WITH_ORIGIN;

	uint32_t max_depth = (msg->depth > 0) ?
		(uint32_t)msg->depth : 0;

	/* Validate xpath module existence */
	if (msg->xpath) {
		const struct ly_ctx *ly_ctx =
			sr_acquire_context(w->conn);
		if (ly_ctx) {
			const char *p = msg->xpath;
			if (*p == '/') p++;
			const char *colon = strchr(p, ':');
			if (colon) {
				char mod[256];
				size_t ml = (size_t)(colon - p);
				if (ml > 0 &&
				    ml < sizeof(mod)) {
					memcpy(mod, p, ml);
					mod[ml] = '\0';
					if (!ly_ctx_get_module_implemented(
						ly_ctx, mod)) {
						sr_release_context(w->conn);
						worker_close_session(sess);
						codec_serialize_errors(
							msg->accept_type,
							"invalid-value",
							"Module not found",
							(char **)&c->data.body,
							&c->data.body_len);
						c->data.status = 404;
						enqueue_completion(w, c);
						return;
					}
				}
			}
			sr_release_context(w->conn);
		}
	}

	const char *xpath_q = msg->xpath ?
		msg->xpath : "/*";
	RC_TRACE("worker sr_get_data %s depth=%u",
	         xpath_q, max_depth);

	sr_data_t *data = NULL;
	int rc = sr_get_data(
		sess, xpath_q, max_depth, 0, opts, &data);

	/* Build response */
	int status = 200;
	char *body = NULL;
	size_t body_len = 0;

	if (rc != SR_ERR_OK) {
		const char *tag = "operation-failed";
		const char *m = sr_strerror(rc);
		if (rc == SR_ERR_INVAL_ARG) {
			status = 400;
			tag = "invalid-value";
			m = "Unknown or unsupported "
			    "datastore";
		} else if (rc == SR_ERR_NOT_FOUND) {
			status = 404;
			tag = "invalid-value";
		} else if (rc == SR_ERR_UNAUTHORIZED) {
			status = 403;
			tag = "access-denied";
		} else {
			status = 500;
		}
		codec_serialize_errors(
			msg->accept_type, tag, m,
			&body, &body_len);
	} else if (data && data->tree) {
		struct lyd_node *filtered = NULL;
		if (msg->fields_expr &&
		    codec_filter_fields(
			data->tree,
			msg->fields_expr,
			&filtered) == 0 &&
		    filtered) {
			if (codec_serialize_data_wd(
				filtered,
				msg->accept_type,
				msg->with_defaults,
				&body,
				&body_len) != 0) {
				status = 500;
				codec_serialize_errors(
					msg->accept_type,
					"operation-failed",
					"Serialization failed",
					&body, &body_len);
			}
			lyd_free_all(filtered);
		} else if (codec_serialize_data_wd(
			data->tree,
			msg->accept_type,
			msg->with_defaults,
			&body, &body_len) != 0) {
			status = 500;
			codec_serialize_errors(
				msg->accept_type,
				"operation-failed",
				"Serialization failed",
				&body, &body_len);
		}
	} else {
		/*
		 * BUG CORRIGE (rapporte par test_crud.py::test_003_get_leaf) :
		 * RFC 8040 Sec 4.3 impose un 404 Not Found quand la ressource
		 * cible d'un GET n'existe pas. `sr_get_data()` renvoie ici un
		 * SUCCES avec un arbre absent/vide quand le xpath demande ne
		 * correspond a aucune donnee presente (ex: une leaf jamais
		 * positionnee) -- ce n'est PAS une erreur sysrepo, mais cote
		 * RESTCONF cela reste neanmoins un 404 classique. 204 No
		 * Content n'appartient pas au vocabulaire des reponses GET de
		 * la RFC (reserve aux edits reussis sans corps : PUT sur une
		 * ressource existante, PATCH, DELETE).
		 */
		status = 404;
		codec_serialize_errors(
			msg->accept_type, "invalid-value",
			"Resource not found", &body, &body_len);
	}

	c->data.status = status;
	c->data.body = (uint8_t *)body;
	c->data.body_len = body_len;
	c->data.etag = worker_compute_etag(
		(const uint8_t *)body, body_len);

	if (data) sr_release_data(data);
	worker_close_session(sess);
	enqueue_completion(w, c);
}

/**
 * @brief Process an EDIT request in the worker thread.
 */
static void process_edit(
	sr_worker_t *w, sr_worker_msg_t *msg)
{
	sr_completion_t *c = calloc(1, sizeof(*c));
	if (!c) return;
	c->type = SR_COMP_EDIT;
	c->edit.cb = msg->cb.edit_cb;
	c->edit.user_data = msg->user_data;

	if (msg->datastore == RC_DS_UNKNOWN) {
		c->edit.status = 400;
		c->edit.error_tag = safe_strdup(
			"invalid-value");
		c->edit.error_msg = safe_strdup(
			"Unknown or unsupported datastore");
		enqueue_completion(w, c);
		return;
	}
	if (msg->datastore == RC_DS_OPERATIONAL ||
	    msg->datastore == RC_DS_INTENDED) {
		c->edit.status = 405;
		c->edit.error_tag = safe_strdup(
			"operation-not-supported");
		c->edit.error_msg = safe_strdup(
			"Cannot edit read-only datastore");
		enqueue_completion(w, c);
		return;
	}

	sr_session_ctx_t *sess = NULL;
	int rc = worker_open_session(
		w, msg->datastore, msg->username, &sess);
	if (rc != SR_ERR_OK) {
		c->edit.status = 500;
		c->edit.error_tag = safe_strdup(
			"operation-failed");
		c->edit.error_msg = safe_strdup(
			sr_strerror(rc));
		enqueue_completion(w, c);
		return;
	}

	/* If-Match conditional edit (RFC 8040 Sec 3.4.1) */
	if (msg->if_match && msg->xpath) {
		sr_data_t *cur = NULL;
		int get_rc = sr_get_data(
			sess, msg->xpath,
			0, 0, 0, &cur);
		char *cur_etag = NULL;

		if (get_rc == SR_ERR_OK && cur &&
		    cur->tree) {
			char *cb = NULL;
			size_t cl = 0;
			if (codec_serialize_data(
				cur->tree,
				msg->accept_type,
				&cb, &cl) == 0) {
				cur_etag =
					worker_compute_etag(
					(const uint8_t *)cb,
					cl);
				free(cb);
			}
		}
		if (cur) sr_release_data(cur);

		bool match = false;
		if (strcmp(msg->if_match, "*") == 0)
			match = (cur_etag != NULL);
		else if (cur_etag)
			match = (strcmp(msg->if_match,
			                cur_etag) == 0);
		free(cur_etag);

		if (!match) {
			worker_close_session(sess);
			c->edit.status = 412;
			c->edit.error_tag = safe_strdup(
				"operation-not-supported");
			c->edit.error_msg = safe_strdup(
				"ETag mismatch");
			enqueue_completion(w, c);
			return;
		}
	}

	int http_status = 204;
	const char *error_tag = "operation-failed";
	const char *error_msg = "Success";

	if (strcmp(msg->method, "DELETE") == 0) {
		if (!msg->xpath || *msg->xpath == '\0') {
			worker_close_session(sess);
			c->edit.status = 405;
			c->edit.error_tag = safe_strdup(
				"operation-not-supported");
			c->edit.error_msg = safe_strdup(
				"Cannot delete root");
			enqueue_completion(w, c);
			return;
		}
		rc = sr_delete_item(
			sess, msg->xpath,
			SR_EDIT_DEFAULT);
		if (rc == SR_ERR_OK)
			rc = sr_apply_changes(sess, 0);
	} else {
		/* POST, PUT, PATCH */
		if (msg->req_type == MEDIA_TYPE_UNKNOWN) {
			worker_close_session(sess);
			c->edit.status = 415;
			c->edit.error_tag = safe_strdup(
				"operation-not-supported");
			c->edit.error_msg = safe_strdup(
				"Unsupported Content-Type");
			enqueue_completion(w, c);
			return;
		}

		struct lyd_node *data = NULL;
		rc = codec_parse_data(
			sess,
			(const char *)msg->body,
			msg->body_len,
			msg->req_type,
			msg->xpath, &data);

		if (rc == 0 && data) {
			const char *default_op = "merge";
			if (strcmp(msg->method,
			           "PUT") == 0)
				default_op = "replace";
			else if (strcmp(msg->method,
			                "POST") == 0) {
				default_op = "merge";
				http_status = 201;
			}

			int set_count = 0;
			rc = worker_set_leaves_recursive(
				sess, data,
				default_op, &set_count);

			if (rc == SR_ERR_OK &&
			    set_count > 0) {
				rc = sr_apply_changes(
					sess, 0);
			} else if (set_count == 0) {
				rc = SR_ERR_VALIDATION_FAILED;
			}
			lyd_free_all(data);
		} else {
			rc = SR_ERR_VALIDATION_FAILED;
		}
	}

	error_msg = sr_strerror(rc);
	if (rc != SR_ERR_OK) {
		if (rc == SR_ERR_UNAUTHORIZED) {
			error_tag = "access-denied";
			http_status = 403;
		} else if (rc == SR_ERR_NOT_FOUND) {
			error_tag = "invalid-value";
			http_status = 404;
		} else if (rc == SR_ERR_EXISTS) {
			error_tag = "data-exists";
			http_status = 409;
		} else if (rc == SR_ERR_VALIDATION_FAILED
		           || rc == SR_ERR_INVAL_ARG) {
			error_tag = "invalid-value";
			http_status = 400;
		} else if (rc == SR_ERR_LOCKED) {
			error_tag = "lock-denied";
			http_status = 409;
		} else if (rc == SR_ERR_UNSUPPORTED) {
			error_tag =
				"operation-not-supported";
			http_status = 501;
		} else {
			error_tag = "operation-failed";
			http_status = 500;
		}
	}

	c->edit.status = http_status;
	c->edit.error_tag = safe_strdup(error_tag);
	c->edit.error_msg = safe_strdup(error_msg);

	worker_close_session(sess);
	enqueue_completion(w, c);
}

/**
 * @brief Process an RPC request in the worker thread.
 */
static void process_rpc(
	sr_worker_t *w, sr_worker_msg_t *msg)
{
	sr_completion_t *c = calloc(1, sizeof(*c));
	if (!c) return;
	c->type = SR_COMP_RPC;
	c->rpc.cb = msg->cb.rpc_cb;
	c->rpc.user_data = msg->user_data;

	if (!msg->rpc_module || !msg->rpc_name) {
		codec_serialize_errors(
			msg->accept_type,
			"invalid-value",
			"Missing RPC module or name",
			(char **)&c->rpc.body,
			&c->rpc.body_len);
		c->rpc.status = 400;
		enqueue_completion(w, c);
		return;
	}

	sr_session_ctx_t *sess = NULL;
	int rc = worker_open_session(
		w, RC_DS_RUNNING, msg->username, &sess);
	if (rc != SR_ERR_OK) {
		codec_serialize_errors(
			msg->accept_type,
			"operation-failed",
			sr_strerror(rc),
			(char **)&c->rpc.body,
			&c->rpc.body_len);
		c->rpc.status = 500;
		enqueue_completion(w, c);
		return;
	}

	const struct ly_ctx *ly_ctx =
		sr_acquire_context(w->conn);
	if (!ly_ctx) {
		worker_close_session(sess);
		codec_serialize_errors(
			msg->accept_type,
			"operation-failed",
			"Cannot acquire libyang context",
			(char **)&c->rpc.body,
			&c->rpc.body_len);
		c->rpc.status = 500;
		enqueue_completion(w, c);
		return;
	}

	char rpc_path[512];
	snprintf(rpc_path, sizeof(rpc_path),
	         "/%s:%s", msg->rpc_module,
	         msg->rpc_name);

	struct lyd_node *input = NULL;
	rc = lyd_new_path(
		NULL, ly_ctx, rpc_path,
		NULL, 0, &input);
	sr_release_context(w->conn);

	if (rc != LY_SUCCESS || !input) {
		worker_close_session(sess);
		codec_serialize_errors(
			msg->accept_type,
			"invalid-value",
			"Invalid RPC path",
			(char **)&c->rpc.body,
			&c->rpc.body_len);
		c->rpc.status = 400;
		enqueue_completion(w, c);
		return;
	}

	if (msg->body && msg->body_len > 0) {
		if (codec_parse_rpc_input(
			sess, input,
			(const char *)msg->body,
			msg->body_len,
			msg->req_type) != 0) {
			lyd_free_all(input);
			worker_close_session(sess);
			codec_serialize_errors(
				msg->accept_type,
				"invalid-value",
				"Invalid RPC input",
				(char **)&c->rpc.body,
				&c->rpc.body_len);
			c->rpc.status = 400;
			enqueue_completion(w, c);
			return;
		}
	}

	sr_data_t *output = NULL;
	rc = sr_rpc_send_tree(sess, input, 0, &output);
	lyd_free_all(input);

	int status = 200;
	char *body = NULL;
	size_t body_len = 0;

	if (rc != SR_ERR_OK) {
		const char *tag = "operation-failed";
		const char *m = sr_strerror(rc);
		if (rc == SR_ERR_UNAUTHORIZED) {
			status = 403;
			tag = "access-denied";
		} else if (rc == SR_ERR_NOT_FOUND) {
			status = 404;
			tag = "invalid-value";
		} else if (rc == SR_ERR_INVAL_ARG) {
			status = 400;
			tag = "invalid-value";
		} else {
			status = 500;
		}
		codec_serialize_errors(
			msg->accept_type, tag, m,
			&body, &body_len);
	} else if (output && output->tree) {
		if (codec_serialize_data(
			output->tree,
			msg->accept_type,
			&body, &body_len) != 0) {
			status = 500;
			codec_serialize_errors(
				msg->accept_type,
				"operation-failed",
				"RPC output serialization "
				"failed",
				&body, &body_len);
		}
	} else {
		status = 204;
	}

	c->rpc.status = status;
	c->rpc.body = (uint8_t *)body;
	c->rpc.body_len = body_len;

	if (output) sr_release_data(output);
	worker_close_session(sess);
	enqueue_completion(w, c);
}

/* ====================================================================
 * Worker thread main loop
 * ==================================================================== */

static void *worker_thread_func(void *arg)
{
	sr_worker_t *w = (sr_worker_t *)arg;

	RC_INFO("sysrepo worker thread started");

	/* Main loop */
	while (w->running) {
		/* Process all queued requests first */
		sr_worker_msg_t *msg;
		while ((msg = dequeue_request(w)) != NULL) {
			if (msg->op == SR_WORKER_SHUTDOWN) {
				free_msg(msg);
				w->running = false;
				break;
			}

			switch (msg->op) {
			case SR_WORKER_GET:
				process_get(w, msg);
				break;
			case SR_WORKER_EDIT:
				process_edit(w, msg);
				break;
			case SR_WORKER_RPC:
				process_rpc(w, msg);
				break;
			default:
				break;
			}
			free_msg(msg);
		}

		if (!w->running) break;

		/* Wait for new requests */
		struct pollfd fds[1];
		fds[0].fd = w->wakeup_pipe[0];
		fds[0].events = POLLIN;

		/* 200ms timeout for periodic checks */
		RC_TRACE("go poll with 200ns timeout");
		int ret = poll(fds, 1, 200);
		if (ret < 0 && errno != EINTR) {
			RC_WARN("worker poll: %s",
			        strerror(errno));
		}
		RC_TRACE("wakeup %d", w->running);

		/* Drain wakeup pipe */
		if (fds[0].revents & POLLIN) {
			char buf[64];
			while (read(w->wakeup_pipe[0],
			            buf, sizeof(buf)) > 0)
				;
		}
	}

	RC_INFO("sysrepo worker thread exited");
	return NULL;
}

/* ====================================================================
 * libevent completion callback
 * ==================================================================== */

/**
 * @brief Called from libevent when the worker posts completions.
 *
 * Drains the eventfd, then processes all completions on the
 * libevent thread (safe to call nghttp2/h2c functions).
 */
static void completion_event_cb(
	evutil_socket_t fd UNUSED,
	short events UNUSED, void *ctx_ptr)
{
	sr_worker_t *w = (sr_worker_t *)ctx_ptr;

	/* Drain the eventfd */
	uint64_t val;
	while (read(w->comp_fd, &val, sizeof(val)) > 0)
		;

	/* Drain all completions */
	pthread_mutex_lock(&w->comp_mutex);
	sr_completion_t *head = w->comp_head;
	w->comp_head = NULL;
	w->comp_tail = NULL;
	pthread_mutex_unlock(&w->comp_mutex);

	while (head) {
		sr_completion_t *c = head;
		head = c->next;

		switch (c->type) {
		case SR_COMP_DATA:
			if (c->data.cb)
				c->data.cb(
					c->data.status,
					c->data.body,
					c->data.body_len,
					c->data.etag,
					c->data.user_data);
			/* body freed by callback */
			c->data.body = NULL;
			free(c->data.etag);
			c->data.etag = NULL;
			break;
		case SR_COMP_EDIT:
			if (c->edit.cb)
				c->edit.cb(
					c->edit.status,
					c->edit.error_tag,
					c->edit.error_msg,
					NULL,
					c->edit.user_data);
			break;
		case SR_COMP_RPC:
			if (c->rpc.cb)
				c->rpc.cb(
					c->rpc.status,
					c->rpc.body,
					c->rpc.body_len,
					c->rpc.user_data);
			c->rpc.body = NULL;
			break;
		}
		free_completion(c);
	}
}

/* ====================================================================
 * Public API
 * ==================================================================== */

sr_worker_t *sr_worker_create(
	struct event_base *base, sr_conn_ctx_t *conn)
{
	sr_worker_t *w = calloc(1, sizeof(*w));
	if (!w) return NULL;

	w->base = base;
	w->conn = conn;
	w->running = true;
	w->wakeup_pipe[0] = -1;
	w->wakeup_pipe[1] = -1;
	w->comp_fd = -1;

	pthread_mutex_init(&w->req_mutex, NULL);
	pthread_cond_init(&w->req_cond, NULL);
	pthread_mutex_init(&w->comp_mutex, NULL);

	/* Create wakeup pipe */
	if (pipe(w->wakeup_pipe) != 0) {
		RC_ERROR("worker: pipe() failed");
		free(w);
		return NULL;
	}

	if (fcntl(w->wakeup_pipe[0], F_SETFL, O_NONBLOCK) < 0) {
		RC_ERROR("worker: nonblock pipe failed");
		close(w->wakeup_pipe[0]);
		close(w->wakeup_pipe[1]);
		free(w);
		return NULL;
	}

	/* Create completion eventfd */
	w->comp_fd = eventfd(0, EFD_NONBLOCK);
	if (w->comp_fd < 0) {
		RC_ERROR("worker: eventfd() failed");
		close(w->wakeup_pipe[0]);
		close(w->wakeup_pipe[1]);
		free(w);
		return NULL;
	}

	/* Register completion eventfd in libevent */
	w->comp_event = event_new(
		base, w->comp_fd,
		EV_READ | EV_PERSIST,
		completion_event_cb, w);
	if (!w->comp_event) {
		RC_ERROR("worker: event_new failed");
		close(w->comp_fd);
		close(w->wakeup_pipe[0]);
		close(w->wakeup_pipe[1]);
		free(w);
		return NULL;
	}
	event_add(w->comp_event, NULL);

	/* Start the worker thread */
	if (pthread_create(&w->thread, NULL,
		worker_thread_func, w) != 0) {
		RC_ERROR("worker: pthread_create failed");
		event_free(w->comp_event);
		close(w->comp_fd);
		close(w->wakeup_pipe[0]);
		close(w->wakeup_pipe[1]);
		free(w);
		return NULL;
	}

	RC_TRACE("worker: started");
	return w;
}

void sr_worker_destroy(sr_worker_t *w)
{
	if (!w) return;

	/* Send shutdown message */
	sr_worker_msg_t *msg = calloc(1, sizeof(*msg));
	if (msg) {
		msg->op = SR_WORKER_SHUTDOWN;
		enqueue_request(w, msg);
	}

	/* Wait for thread to exit */
	pthread_join(w->thread, NULL);

	/* Cleanup libevent event */
	if (w->comp_event)
		event_free(w->comp_event);

	/* Close FDs */
	if (w->comp_fd >= 0)
		close(w->comp_fd);
	if (w->wakeup_pipe[0] >= 0)
		close(w->wakeup_pipe[0]);
	if (w->wakeup_pipe[1] >= 0)
		close(w->wakeup_pipe[1]);

	/* Drain any remaining completions */
	sr_completion_t *c;
	pthread_mutex_lock(&w->comp_mutex);
	c = w->comp_head;
	w->comp_head = NULL;
	w->comp_tail = NULL;
	pthread_mutex_unlock(&w->comp_mutex);
	while (c) {
		sr_completion_t *next = c->next;
		free_completion(c);
		c = next;
	}

	/* Drain any remaining requests */
	sr_worker_msg_t *m;
	while ((m = dequeue_request(w)) != NULL)
		free_msg(m);

	pthread_mutex_destroy(&w->req_mutex);
	pthread_cond_destroy(&w->req_cond);
	pthread_mutex_destroy(&w->comp_mutex);

	free(w);
}

void sr_worker_submit_get(
	sr_worker_t *w,
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
	void *user_data)
{
	sr_worker_msg_t *msg = calloc(1, sizeof(*msg));
	if (!msg) {
		/* OOM: call callback with error */
		callback(500, NULL, 0, NULL, user_data);
		return;
	}

	msg->op = SR_WORKER_GET;
	msg->cb.data_cb = callback;
	msg->user_data = user_data;
	msg->res_type = res_type;
	msg->datastore = datastore;
	msg->username = safe_strdup(username);
	msg->xpath = safe_strdup(xpath);
	msg->content_filter = safe_strdup(content_filter);
	msg->depth = depth;
	msg->fields_expr = safe_strdup(fields_expr);
	msg->with_defaults = safe_strdup(with_defaults);
	msg->with_origin = with_origin;
	msg->accept_type = accept_type;

	enqueue_request(w, msg);
}

void sr_worker_submit_edit(
	sr_worker_t *w,
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
	void *user_data)
{
	sr_worker_msg_t *msg = calloc(1, sizeof(*msg));
	if (!msg) {
		callback(500, "operation-failed",
		         "Memory allocation failed",
		         NULL, user_data);
		return;
	}

	msg->op = SR_WORKER_EDIT;
	msg->cb.edit_cb = callback;
	msg->user_data = user_data;
	msg->datastore = datastore;
	msg->username = safe_strdup(username);
	msg->xpath = safe_strdup(xpath);
	msg->method = safe_strdup(method);
	msg->req_type = req_type;
	msg->accept_type = accept_type;
	msg->if_match = safe_strdup(if_match);

	if (body && body_len > 0) {
		msg->body = malloc(body_len);
		if (msg->body) {
			memcpy(msg->body, body, body_len);
			msg->body_len = body_len;
		}
	}


	enqueue_request(w, msg);
}

void sr_worker_submit_rpc(
	sr_worker_t *w,
	const char *username,
	const char *rpc_module,
	const char *rpc_name,
	media_type_t req_type,
	media_type_t accept_type,
	const uint8_t *body,
	size_t body_len,
	plugin_rpc_cb callback,
	void *user_data)
{
	sr_worker_msg_t *msg = calloc(1, sizeof(*msg));
	if (!msg) {
		callback(500, NULL, 0, user_data);
		return;
	}

	msg->op = SR_WORKER_RPC;
	msg->cb.rpc_cb = callback;
	msg->user_data = user_data;
	msg->username = safe_strdup(username);
	msg->rpc_module = safe_strdup(rpc_module);
	msg->rpc_name = safe_strdup(rpc_name);
	msg->req_type = req_type;
	msg->accept_type = accept_type;

	if (body && body_len > 0) {
		msg->body = malloc(body_len);
		if (msg->body) {
			memcpy(msg->body, body, body_len);
			msg->body_len = body_len;
		}
	}

	enqueue_request(w, msg);
}

const struct ly_ctx *sr_worker_acquire_ly_ctx(
	sr_worker_t *w)
{
	if (!w || !w->conn) return NULL;
	return sr_acquire_context(w->conn);
}

void sr_worker_release_ly_ctx(sr_worker_t *w)
{
	if (w && w->conn)
		sr_release_context(w->conn);
}
