#define _GNU_SOURCE
#define UNUSED __attribute__((unused))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libyang/libyang.h>
#include <sysrepo.h>
#include "codec.h"
#include "logger.h"

media_type_t codec_parse_content_type(const char *header)
{
	if (!header) return MEDIA_TYPE_UNKNOWN;
	/* RFC 8040 Sec 4.6.1 : Plain Patch utilise les mêmes media
	 * types que les autres opérations (application/yang-data+json
	 * ou application/yang-data+xml).
	 *
	 * RFC 8072 Sec 2.1 : YANG Patch utilise
	 * application/yang-patch+json ou application/yang-patch+xml.
	 *
	 * Certains clients envoient aussi
	 * application/yang-data+patch+json par erreur — l'accepter
	 * comme JSON pour la toléranceinteropérabilité. */
	if (strstr(header, "application/yang-data+json") ||
	    strstr(header, "application/yang-patch+json") ||
	    strstr(header, "application/yang-data+patch+json")) {
		return MEDIA_TYPE_JSON;
	}
	if (strstr(header, "application/yang-data+xml") ||
	    strstr(header, "application/yang-patch+xml") ||
	    strstr(header, "application/yang-data+patch+xml")) {
		return MEDIA_TYPE_XML;
	}
	return MEDIA_TYPE_UNKNOWN;
}

media_type_t codec_parse_accept(const char *header)
{
	if (!header) return MEDIA_TYPE_JSON;
	if (strstr(header, "application/yang-data+xml")) {
		return MEDIA_TYPE_XML;
	}
	if (strstr(header, "application/yang-data+json")) {
		return MEDIA_TYPE_JSON;
	}
	return MEDIA_TYPE_JSON;
}

int codec_serialize_data(
	const struct lyd_node *tree,
	media_type_t type,
	char **out_buf,
	size_t *out_len)
{
	return codec_serialize_data_wd(
		tree, type, NULL, out_buf, out_len);
}

int codec_serialize_data_wd(
	const struct lyd_node *tree,
	media_type_t type,
	const char *with_defaults,
	char **out_buf,
	size_t *out_len)
{
	if (!tree || !out_buf || !out_len) return -1;

	LYD_FORMAT ly_fmt = (type == MEDIA_TYPE_XML) ?
		LYD_XML : LYD_JSON;

	uint32_t options = LYD_PRINT_SIBLINGS | LYD_PRINT_SHRINK;

	/* RFC 8040 Sec 4.8.9 : with-defaults */
	if (with_defaults) {
		if (strcmp(with_defaults, "report-all") == 0) {
			options |= LYD_PRINT_WD_ALL;
		} else if (strcmp(with_defaults,
		                  "report-all-tagged") == 0) {
			options |= LYD_PRINT_WD_ALL_TAG;
		} else if (strcmp(with_defaults, "trim") == 0) {
			options |= LYD_PRINT_WD_TRIM;
		} else if (strcmp(with_defaults,
		                  "explicit") == 0) {
			options |= LYD_PRINT_WD_EXPLICIT;
		}
	} else 
		options |= LYD_PRINT_WD_ALL;

	if (lyd_print_mem(out_buf, tree, ly_fmt, options) != 0) {
		return -1;
	}

	*out_len = strlen(*out_buf);
	return 0;
}

/**
 * @brief Return a malloc'd copy of @p xpath with its last top-level
 * path segment stripped off, i.e. the XPath of the immediate parent
 * of the resource @p xpath designates.
 *
 * Bracket depth is tracked so list/leaf-list predicates such as
 * "[name='eth0']" (which never themselves contain a path separator
 * in the XPath syntax produced by router.c) do not confuse the
 * scan; kept generic regardless.
 *
 * @param[in] xpath Absolute XPath (as built by router.c), e.g.
 *            "/restconf-test:system/config" or
 *            "/restconf-test:interfaces/interface[name='eth0']".
 *
 * @return Malloc'd parent XPath (caller frees), or NULL if
 *         @p xpath is NULL/empty or already designates a top-level
 *         resource (single path segment).
 */
static char *codec_xpath_parent(const char *xpath)
{
	if (!xpath || xpath[0] != '/') return NULL;

	int depth = 0;
	const char *last_slash = NULL;

	for (const char *p = xpath; *p; p++) {
		if (*p == '[') {
			depth++;
		} else if (*p == ']') {
			if (depth > 0) depth--;
		} else if (*p == '/' && depth == 0) {
			last_slash = p;
		}
	}

	/* No top-level '/' other than the leading one: single segment,
	 * i.e. the resource is already top-level (e.g. "/oven:oven"). */
	if (!last_slash || last_slash == xpath) return NULL;

	size_t len = (size_t)(last_slash - xpath);
	char *out = malloc(len + 1);
	if (!out) return NULL;
	memcpy(out, xpath, len);
	out[len] = '\0';
	return out;
}

int codec_parse_data(
	sr_session_ctx_t *session,
	const char *payload,
	size_t len,
	media_type_t type,
	const char *xpath,
	struct lyd_node **tree)
{
	if (!session || !payload || !tree) return -1;

	LYD_FORMAT ly_fmt = (type == MEDIA_TYPE_XML) ?
		LYD_XML : LYD_JSON;

	const struct ly_ctx *ly_ctx = sr_acquire_context(
		sr_session_get_connection(session));
	if (!ly_ctx) return -1;

	/*
	 * RFC 8040 Sec 4.5 (PUT) : Quand on édite une data resource
	 * spécifique (ex: /oven:oven), on ne doit PAS valider le module
	 * entier car cela inclurait les state nodes (config false) qui
	 * ne font pas partie de l'édition.
	 *
	 * LYD_PARSE_ONLY : Parse la syntaxe sans validation complète.
	 * La validation sera effectuée par sysrepo lors de
	 * sr_apply_changes() au niveau du datastore, avec les bons
	 * flags pour ignorer les state nodes non concernés.
	 *
	 * Cela évite l'erreur "Unexpected data state node found" quand
	 * le module contient à la fois des containers de configuration
	 * et de state data au niveau supérieur.
	 */
	uint32_t parse_opts = LYD_PARSE_STRICT | LYD_PARSE_OPAQ | LYD_PARSE_ONLY;
	uint32_t validate_opts = 0;

	/* lyd_parse_data (via ly_in) expects a NUL-terminated string,
	 * like the old lyd_parse_data_mem() call it replaces. */
	char *null_term = malloc(len + 1);
	if (!null_term) {
		sr_release_context(
			sr_session_get_connection(session));
		return -1;
	}
	memcpy(null_term, payload, len);
	null_term[len] = '\0';

	/*
	 * RFC 8040 §4.5 : le corps encode la ressource CIBLE elle-même
	 * ("module:target-name": {...}). Si cette ressource est
	 * imbriquée (pas un nœud top-level du module), ce nom n'existe
	 * comme nœud de schéma qu'en tant qu'enfant de son parent réel :
	 * on construit donc d'abord un squelette vide de ses ancêtres
	 * via lyd_new_path(), pour ensuite parser le corps comme
	 * sous-arbre rattaché sous cet ancêtre (cf. codec_parse_data()
	 * dans codec.h pour le détail du problème corrigé ici).
	 */
	struct lyd_node *parent_node = NULL;
	char *parent_path = xpath ? codec_xpath_parent(xpath) : NULL;

	if (parent_path) {
		LY_ERR anc_rc = lyd_new_path(
			NULL, ly_ctx, parent_path, NULL, 0, &parent_node);

		if (anc_rc != LY_SUCCESS || !parent_node) {
			RC_ERROR("codec_parse_data: failed to build ancestor "
			         "skeleton for '%s' (rc=%d)",
			         parent_path, anc_rc);
			free(parent_path);
			free(null_term);
			sr_release_context(
				sr_session_get_connection(session));
			return -1;
		}

		/* lyd_new_path() only guarantees returning "a" node of the
		 * newly created chain, not necessarily the deepest one when
		 * @p parent_path spans multiple levels; re-resolve the exact
		 * ancestor node deterministically instead of relying on it. */
		struct lyd_node *resolved = NULL;
		if (lyd_find_path(parent_node, parent_path, 0, &resolved) ==
		    LY_SUCCESS && resolved) {
			parent_node = resolved;
		}
		free(parent_path);
	}

	struct ly_in *in = NULL;
	if (ly_in_new_memory(null_term, &in) != LY_SUCCESS || !in) {
		if (parent_node) {
			struct lyd_node *root = parent_node;
			while (lyd_parent(root)) root = lyd_parent(root);
			lyd_free_all(root);
		}
		free(null_term);
		sr_release_context(
			sr_session_get_connection(session));
		return -1;
	}

	struct lyd_node *parsed = NULL;
	LY_ERR ly_rc = lyd_parse_data(
	        ly_ctx, parent_node, in, ly_fmt,
	        parse_opts, validate_opts, &parsed);

	ly_in_free(in, 0);

	/* Note: avec LYD_PARSE_ONLY, la validation est différée à
	 * sr_apply_changes() qui utilise les flags appropriés pour
	 * ignorer les state nodes non concernés par l'édition. */

	if (ly_rc != LY_SUCCESS) {
		/* Log detailed libyang error for debugging */
		const struct ly_err_item *err = ly_err_first(ly_ctx);
		if (err) {
			RC_ERROR("codec_parse_data: libyang error: %s",
			         err->msg ? err->msg : "(no message)");
			if (err->data_path) {
				RC_ERROR("codec_parse_data: at path: %s",
				         err->data_path);
			}
		} else {
			RC_ERROR("codec_parse_data: libyang error "
			         "(no details available)");
		}
		if (parent_node) {
			struct lyd_node *root = parent_node;
			while (lyd_parent(root)) root = lyd_parent(root);
			lyd_free_all(root);
		} else if (parsed) {
			lyd_free_all(parsed);
		}
		free(null_term);
		sr_release_context(
			sr_session_get_connection(session));
		return -1;
	}

	if (parent_node) {
		/* Return the root of the (ancestor skeleton + parsed
		 * subtree) tree as a single unit for the caller to walk
		 * (plugin_set_leaves_recursive() safely skips non-leaf
		 * ancestor containers) and free via lyd_free_all(). */
		struct lyd_node *root = parent_node;
		while (lyd_parent(root)) root = lyd_parent(root);
		*tree = root;
	} else {
		*tree = parsed;
	}

	free(null_term);
	sr_release_context(
		sr_session_get_connection(session));
	return 0;
}

int codec_parse_rpc_input(
	sr_session_ctx_t *session,
	struct lyd_node *op_node,
	const char *payload,
	size_t len,
	media_type_t type)
{
	if (!session || !op_node || !payload) return -1;

	LYD_FORMAT ly_fmt = (type == MEDIA_TYPE_XML) ?
		LYD_XML : LYD_JSON;

	const struct ly_ctx *ly_ctx = sr_acquire_context(
		sr_session_get_connection(session));
	if (!ly_ctx) return -1;

	/* lyd_parse_op() (via ly_in) expects a NUL-terminated
	 * string, just like lyd_parse_data_mem() above. */
	char *null_term = malloc(len + 1);
	if (!null_term) {
		sr_release_context(
			sr_session_get_connection(session));
		return -1;
	}
	memcpy(null_term, payload, len);
	null_term[len] = '\0';

	struct ly_in *in = NULL;
	if (ly_in_new_memory(null_term, &in) != LY_SUCCESS || !in) {
		free(null_term);
		sr_release_context(
			sr_session_get_connection(session));
		return -1;
	}

	/*
	 * RFC 8040 Sec 3.6.1: the RESTCONF-encoded RPC/action input
	 * is "module:input" (JSON) or <input> (XML). For
	 * LYD_TYPE_RPC_RESTCONF, libyang requires "parent" to be the
	 * already-created operation node (op_node): the parsed input
	 * children are appended directly under it, and the "op"
	 * output parameter MUST be NULL in that case. "tree" still
	 * has to be provided: it receives the separate RESTCONF
	 * opaque envelope tree (if any), which we don't need but
	 * must free.
	 */
	struct lyd_node *envelope = NULL;
	LY_ERR rc = lyd_parse_op(
		ly_ctx, op_node, in, ly_fmt,
		LYD_TYPE_RPC_RESTCONF, LYD_PARSE_STRICT, &envelope, NULL);

	ly_in_free(in, 0);
	free(null_term);

	if (envelope) lyd_free_all(envelope);

	sr_release_context(sr_session_get_connection(session));
	return (rc == LY_SUCCESS) ? 0 : -1;
}

int codec_serialize_errors(
	media_type_t type,
	const char *error_tag,
	const char *error_msg,
	char **out_buf,
	size_t *out_len)
{
	char *buf = NULL;
	int len = 0;
	const char *tag = error_tag ?
		error_tag : "operation-failed";
	const char *msg = error_msg ?
		error_msg : "Unknown error";

	if (type == MEDIA_TYPE_XML) {
		len = asprintf(&buf,
			"<errors xmlns=\"urn:ietf:params:xml:ns:"
			"yang:ietf-restconf\"><error>"
			"<error-type>application</error-type>"
			"<error-tag>%s</error-tag>"
			"<error-message>%s</error-message>"
			"</error></errors>", tag, msg);
	} else {
		len = asprintf(&buf,
			"{\"ietf-restconf:errors\":{\"error\":[{"
			"\"error-type\":\"application\","
			"\"error-tag\":\"%s\","
			"\"error-message\":\"%s\"}]}}",
			tag, msg);
	}

	if (len < 0 || !buf) return -1;
	*out_buf = buf;
	*out_len = (size_t)len;
	return 0;
}

/**
 * @brief Check whether the given node name appears in a
 * semicolon-separated field list. Returns 1 if found, 0 otherwise.
 */
static int field_in_list(
	const char *node_name, const char *fields)
{
	if (!fields || !*fields) return 1;

	char *copy = strdup(fields);
	if (!copy) return 1;

	char *saveptr = NULL;
	char *tok = strtok_r(copy, ";", &saveptr);

	while (tok) {
		/* Handle sub-paths: "parent/child" */
		char *slash = strchr(tok, '/');
		char *name = tok;
		if (slash) *slash = '\0';

		/* Handle parentheses: "name(sub)" */
		char *paren = strchr(name, '(');
		if (paren) *paren = '\0';

		/* Trim leading/trailing spaces */
		while (*name == ' ') name++;
		char *end = name + strlen(name) - 1;
		while (end > name && *end == ' ') {
			*end = '\0';
			end--;
		}

		if (strcmp(name, node_name) == 0) {
			free(copy);
			return 1;
		}
		tok = strtok_r(NULL, ";", &saveptr);
	}

	free(copy);
	return 0;
}

int codec_filter_fields(
	const struct lyd_node *tree,
	const char *fields_expr,
	struct lyd_node **out_tree)
{
	if (!tree || !fields_expr || !out_tree) return -1;

	/* Duplicate the tree to avoid modifying the original.
	 * lyd_dup_siblings with LYD_DUP_RECURSIVE duplicates the node,
	 * its siblings, and all their children recursively.
	 * Metadata is duplicated by default. */
	struct lyd_node *dup = NULL;
	if (lyd_dup_siblings(tree, NULL, LYD_DUP_RECURSIVE, &dup) != LY_SUCCESS) {
		*out_tree = NULL;
		return -1;
	}

	/* Filter top-level nodes */
	struct lyd_node *node = dup;
	struct lyd_node *prev = NULL;

	while (node) {
		struct lyd_node *next = node->next;

		if (!field_in_list(node->schema->name,
		                   fields_expr)) {
			/* Remove this node from the list */
			if (prev) {
				prev->next = next;
			} else {
				dup = next;
			}
			lyd_free_tree(node);
		} else {
			prev = node;
		}
		node = next;
	}

	*out_tree = dup;
	return 0;
}
