#define _GNU_SOURCE
#define UNUSED __attribute__((unused))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libyang/libyang.h>
#include <sysrepo.h>
#include "codec.h"

media_type_t codec_parse_content_type(const char *header)
{
	if (!header) return MEDIA_TYPE_UNKNOWN;
	if (strstr(header, "application/yang-data+json")) {
		return MEDIA_TYPE_JSON;
	}
	if (strstr(header, "application/yang-data+xml")) {
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
	}

	if (lyd_print_mem(out_buf, tree, ly_fmt, options) != 0) {
		return -1;
	}

	*out_len = strlen(*out_buf);
	return 0;
}

int codec_parse_data(
	sr_session_ctx_t *session,
	const char *payload,
	size_t len,
	media_type_t type,
	struct lyd_node **tree)
{
	if (!session || !payload || !tree) return -1;

	LYD_FORMAT ly_fmt = (type == MEDIA_TYPE_XML) ?
		LYD_XML : LYD_JSON;
	
	const struct ly_ctx *ly_ctx = sr_acquire_context(
		sr_session_get_connection(session));
	if (!ly_ctx) return -1;

	uint32_t parse_opts = LYD_PARSE_STRICT | LYD_PARSE_OPAQ;
	uint32_t validate_opts = LYD_VALIDATE_PRESENT;

	/* lyd_parse_data_mem attend une chaîne terminée par \0 */
	char *null_term = malloc(len + 1);
	if (!null_term) {
		sr_release_context(
			sr_session_get_connection(session));
		return -1;
	}
	memcpy(null_term, payload, len);
	null_term[len] = '\0';

	if (lyd_parse_data_mem(
	        ly_ctx, null_term, ly_fmt,
	        parse_opts, validate_opts, tree) != 0) {
		free(null_term);
		sr_release_context(
			sr_session_get_connection(session));
		return -1;
	}
	
	free(null_term);
	sr_release_context(
		sr_session_get_connection(session));
	return 0;
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
 * @brief Vérifie si un nom de nœud est dans la liste des
 * champs autorisés (séparés par ';').
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
		/* Gérer les sous-chemins : "parent/child" */
		char *slash = strchr(tok, '/');
		char *name = tok;
		if (slash) *slash = '\0';

		/* Gérer les parenthèses : "name(sub)" */
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

	/* Dupliquer l'arbre pour ne pas modifier l'original.
	 * lyd_dup_siblings avec LYD_DUP_RECURSIVE duplique le nœud,
	 * ses frères et sœurs, et tous leurs enfants récursivement.
	 * Les métadonnées sont dupliquées par défaut. */
	struct lyd_node *dup = NULL;
	if (lyd_dup_siblings(tree, NULL, LYD_DUP_RECURSIVE, &dup) != LY_SUCCESS) {
		*out_tree = NULL;
		return -1;
	}

	/* Filtrer les nœuds de premier niveau */
	struct lyd_node *node = dup;
	struct lyd_node *prev = NULL;

	while (node) {
		struct lyd_node *next = node->next;

		if (!field_in_list(node->schema->name,
		                   fields_expr)) {
			/* Retirer ce nœud de la liste */
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
