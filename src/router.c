#define _GNU_SOURCE
#define UNUSED __attribute__((unused))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libyang/libyang.h>
#include "router.h"

#define MAX_XPATH_LEN 4096
#define MAX_DECODED_LEN 512

/**
 * @brief Resolve a YANG prefix to its module name.
 *
 * Given a string that may be either a YANG prefix (e.g. "rt")
 * or a module name (e.g. "restconf-test"), this function
 * returns the actual module name. If @p prefix is already a
 * valid module name, it is returned as-is. Otherwise, all
 * implemented modules are scanned for a matching prefix.
 *
 * @param[in] ctx libyang context (may be NULL).
 * @param[in] prefix YANG prefix or module name from the URI.
 *
 * @return Module name (points inside @p ctx, do not free),
 *         or @p prefix itself if resolution fails or @p ctx
 *         is NULL.
 */
static const char *resolve_module_name(
	const struct ly_ctx *ctx, const char *prefix)
{
	if (!ctx || !prefix) return prefix;

	/* Fast path: prefix IS the module name */
	const struct lys_module *mod =
		ly_ctx_get_module_implemented(ctx, prefix);
	if (mod) return mod->name;

	/* Slow path: scan all modules for a matching prefix */
	uint32_t idx = 0;
	while ((mod = ly_ctx_get_module_iter(ctx, &idx))) {
		if (mod->implemented && mod->prefix &&
		    strcmp(mod->prefix, prefix) == 0) {
			return mod->name;
		}
	}
	/* Resolution failed: return the original string */
	return prefix;
}

/**
 * @brief Decode a percent-encoded string (RFC 3986).
 * Returns -1 if the percent-encoding is invalid.
 */
static int percent_decode(
	const char *src, size_t src_len,
	char *dst, size_t dst_size)
{
	size_t i = 0, j = 0;
	while (i < src_len && j < dst_size - 1) {
		if (src[i] == '%' && i + 2 < src_len) {
			char hex[3] = { src[i+1], src[i+2], '\0' };
			char *endptr;
			long val = strtol(hex, &endptr, 16);
			if (*endptr == '\0') {
				dst[j++] = (char)val;
				i += 3;
			} else {
				/* Invalid percent-encoding (e.g., %%%) */
				return -1;
			}
		} else if (src[i] == '%') {
			/* % isolated at end of string */
			return -1;
		} else {
			dst[j++] = src[i++];
		}
	}
	dst[j] = '\0';
	return 0;
}

/**
 * @brief Validate percent-encoding of a URI string (RFC 3986).
 * Returns 0 if valid, -1 if invalid (e.g., %%% or isolated %).
 */
static int validate_percent_encoding(const char *src, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (src[i] == '%') {
			/* Must be followed by 2 hexadecimal characters */
			if (i + 2 >= len) return -1;
			if (!isxdigit((unsigned char)src[i+1]) ||
			    !isxdigit((unsigned char)src[i+2])) {
				return -1;
			}
			i += 2;
		}
	}
	return 0;
}

/**
 * @brief Build the XPath predicate for a list or leaf-list.
 * Uses the YANG schema to find key names.
 *
 * @param[in] ctx libyang context.
 * @param[in] node_path Full schema/data path of the list or
 *            leaf-list node whose predicate is being built (e.g.
 *            "/mod:top/mod:list1"). The caller has ALREADY
 *            appended "/module:node" to @p out_buf before calling
 *            this function (cf. parse_segment()), so @p node_path
 *            and the current content of @p out_buf are the same
 *            string: it MUST be used as-is for the schema lookup,
 *            not rebuilt from a module/node pair, or the resulting
 *            path would be duplicated (".../mod:node/mod:node")
 *            and lys_find_path() would always fail (cf. 4.1, 4.2).
 * @param[in] values_str Raw (still percent-encoded) key value(s)
 *            string taken from the URI, e.g. "key1,key2" or a
 *            single leaf-list value.
 * @param[in,out] out_buf XPath buffer being built (same string as
 *            @p node_path); the predicate is appended to it.
 * @param[in,out] out_len Current length of @p out_buf, updated in
 *            place as the predicate is appended.
 *
 * @return 0 on success, -1 on error (unknown node, or '=' used on
 *         a non-list/leaf-list node).
 */
static int build_xpath_predicate(
	const struct ly_ctx *ctx,
	const char *node_path,
	const char *values_str,
	char *out_buf, size_t *out_len)
{
	int written;

	const struct lysc_node *node = lys_find_path(
		ctx, NULL, node_path, 0);

	if (!node) {
		/* Fallback if schema is not loaded */
		return -1;
	}

	if (node->nodetype == LYS_LEAFLIST) {
		char decoded[MAX_DECODED_LEN];
		percent_decode(values_str, strlen(values_str),
		               decoded, sizeof(decoded));
		written = snprintf(out_buf + *out_len,
		                   MAX_XPATH_LEN - *out_len,
		                   "[.='%s']", decoded);
		if (written < 0) return -1;
		*out_len += written;
	} else if (node->nodetype == LYS_LIST) {
		const struct lysc_node_list *list =
			(const struct lysc_node_list *)node;
		const struct lysc_node *key = list->child;
		char *val_ptr = (char *)values_str;

		while (key && val_ptr) {
			if (key->flags & LYS_KEY) {
				char *comma = strchr(val_ptr, ',');
				size_t val_len = comma ?
					(size_t)(comma - val_ptr) :
					strlen(val_ptr);

				char decoded[MAX_DECODED_LEN];
				percent_decode(val_ptr, val_len,
				               decoded, sizeof(decoded));

				written = snprintf(
					out_buf + *out_len,
					MAX_XPATH_LEN - *out_len,
					"[%s='%s']", key->name, decoded);
				if (written < 0) return -1;
				*out_len += written;

				if (!comma) break;
				val_ptr = comma + 1;
			}
			key = key->next;
		}
	} else {
		return -1; /* Error: '=' used on a non-list/leaf-list node */
	}
	return 0;
}

/**
 * @brief Parse one URI segment and append it to the XPath.
 */
static int parse_segment(
	const struct ly_ctx *ctx,
	char *current_xpath, size_t *xpath_len,
	const char *segment, size_t seg_len)
{
	char seg_buf[1024];
	if (seg_len >= sizeof(seg_buf)) return -1;
	memcpy(seg_buf, segment, seg_len);
	seg_buf[seg_len] = '\0';

	char *eq_pos = strchr(seg_buf, '=');
	char *colon_pos = strchr(seg_buf, ':');

	char *module_name = NULL;
	char *node_name = seg_buf;

	if (colon_pos && (!eq_pos || colon_pos < eq_pos)) {
		*colon_pos = '\0';
		/* Resolve YANG prefix to module name (cf. 4.1) */
		module_name = (char *)resolve_module_name(
			ctx, seg_buf);
		node_name = colon_pos + 1;
	}

	if (eq_pos) {
		*eq_pos = '\0';
		char *values_str = eq_pos + 1;

		/* Ajouter /module:node ou /node */
		int written;
		if (module_name) {
			written = snprintf(
				current_xpath + *xpath_len,
				MAX_XPATH_LEN - *xpath_len,
				"/%s:%s", module_name, node_name);
		} else {
			written = snprintf(
				current_xpath + *xpath_len,
				MAX_XPATH_LEN - *xpath_len,
				"/%s", node_name);
		}
		if (written < 0) return -1;
		*xpath_len += written;

		/* Ajouter le prédicat [key='val'] : current_xpath
		 * contient déjà le chemin complet du nœud ("/mod:node"
		 * qu'on vient d'ajouter ci-dessus), on le réutilise tel
		 * quel pour la résolution du schéma (cf. 4.1, 4.2). */
		if (ctx) {
			return build_xpath_predicate(
				ctx, current_xpath,
				values_str,
				current_xpath, xpath_len);
		}
		return -1; /* Contexte requis pour les listes */
	} else {
		/* Pas de '=', c'est un container, leaf ou racine */
		int written;
		if (module_name) {
			written = snprintf(
				current_xpath + *xpath_len,
				MAX_XPATH_LEN - *xpath_len,
				"/%s:%s", module_name, node_name);
		} else {
			written = snprintf(
				current_xpath + *xpath_len,
				MAX_XPATH_LEN - *xpath_len,
				"/%s", node_name);
		}
		if (written < 0) return -1;
		*xpath_len += written;
	}
	return 0;
}

/**
 * @brief Parse les query parameters RESTCONF (RFC 8040 Sec 4.8).
 * Extrait content, depth, fields, with-defaults, with-origin.
 */
static int parse_query_params(
	const char *query, rc_request_t *req)
{
	if (!query || !*query) return 0;

	char *q_copy = strdup(query);
	if (!q_copy) return -1;

	char *saveptr = NULL;
	char *param = strtok_r(q_copy, "&", &saveptr);

	while (param) {
		char *eq = strchr(param, '=');
		if (!eq) {
			/* Paramètre sans valeur (ex: "with-origin") */
			if (strcmp(param, "with-origin") == 0) {
				req->with_origin = true;
			}
			param = strtok_r(NULL, "&", &saveptr);
			continue;
		}

		*eq = '\0';
		const char *key = param;
		const char *val = eq + 1;

		if (strcmp(key, "content") == 0) {
			req->content_filter = strdup(val);
		} else if (strcmp(key, "depth") == 0) {
			if (strcmp(val, "unbounded") == 0) {
				req->depth = -1;
			} else {
				req->depth = atoi(val);
			}
		} else if (strcmp(key, "fields") == 0) {
			req->fields_expr = strdup(val);
		} else if (strcmp(key, "with-defaults") == 0) {
			req->with_defaults = strdup(val);
		} else if (strcmp(key, "with-origin") == 0) {
			req->with_origin = true;
		}

		param = strtok_r(NULL, "&", &saveptr);
	}

	free(q_copy);
	return 0;
}

int router_parse_request(
	const struct ly_ctx *ctx,
	const char *path, const char *method,
	const char *auth_header UNUSED,
	const char *content_type,
	const char *accept,
	const char *if_match,
	rc_request_t *req_out)
{
	if (!path || !method || !req_out) return -1;

	memset(req_out, 0, sizeof(rc_request_t));
	req_out->method = method;
	req_out->depth = -1;
	req_out->req_type = codec_parse_content_type(content_type);
	req_out->accept_type = codec_parse_accept(accept);

	/* RFC 8040 Sec 3.4.1: If-Match pour les edits conditionnels */
	if (if_match && *if_match) {
		req_out->if_match = strdup(if_match);
	}

	/* Split path et query string (RFC 8040 Sec 3.5.1) */
	char path_buf[2048];
	const char *query = NULL;
	const char *qmark = strchr(path, '?');

	if (qmark) {
		size_t path_len = (size_t)(qmark - path);
		if (path_len >= sizeof(path_buf)) return -1;
		memcpy(path_buf, path, path_len);
		path_buf[path_len] = '\0';
		query = qmark + 1;
		path = path_buf;
	}

	if (strcmp(path, "/.well-known/host-meta") == 0) {
		req_out->res_type = RC_RES_ROOT_DISCOVERY;
		return 0;
	}
	if (strcmp(path, "/.well-known/host-meta.json") == 0) {
		req_out->res_type = RC_RES_ROOT_DISCOVERY_JSON;
		return 0;
	}
	if (strcmp(path, "/restconf") == 0) {
		req_out->res_type = RC_RES_API;
		return 0;
	}

	const char *rest_path = NULL;
	if (strcmp(path, "/restconf/data") == 0 ||
	    strncmp(path, "/restconf/data/", 15) == 0) {
		req_out->res_type = RC_RES_DATA;
		/* RFC 8040: /restconf/data cible running par défaut */
		req_out->datastore = RC_DS_RUNNING;
		rest_path = (path[14] == '/') ? path + 15 : "";
	} else if (strcmp(path, "/restconf/ds") == 0) {
		/* RFC 8527: /restconf/ds sans datastore = erreur */
		req_out->res_type = RC_RES_DS;
		req_out->datastore = RC_DS_UNKNOWN;
		rest_path = "";
	} else if (strncmp(path, "/restconf/ds/", 13) == 0) {
		req_out->res_type = RC_RES_DS;
		const char *ds_start = path + 13;
		const char *ds_end = strchr(ds_start, '/');
		if (ds_end) {
			/* Extraire l'identityref datastore (RFC 8527) */
			size_t ds_len = (size_t)(ds_end - ds_start);
			char ds_identity[256];
			if (ds_len >= sizeof(ds_identity)) return -1;
			memcpy(ds_identity, ds_start, ds_len);
			ds_identity[ds_len] = '\0';

			/* Mapper l'identityref vers rc_datastore_t */
			if (strstr(ds_identity, "running")) {
				req_out->datastore = RC_DS_RUNNING;
			} else if (strstr(ds_identity, "operational")) {
				req_out->datastore = RC_DS_OPERATIONAL;
			} else if (strstr(ds_identity, "intended")) {
				req_out->datastore = RC_DS_INTENDED;
			} else {
				req_out->datastore = RC_DS_UNKNOWN;
			}
			rest_path = ds_end + 1;
		} else {
			/* /restconf/ds/<datastore> sans slash final
			 * (racine du datastore NMDA) */
			size_t ds_len = strlen(ds_start);
			char ds_identity[256];
			if (ds_len >= sizeof(ds_identity)) return -1;
			memcpy(ds_identity, ds_start, ds_len);
			ds_identity[ds_len] = '\0';

			if (strstr(ds_identity, "running")) {
				req_out->datastore = RC_DS_RUNNING;
			} else if (strstr(ds_identity, "operational")) {
				req_out->datastore = RC_DS_OPERATIONAL;
			} else if (strstr(ds_identity, "intended")) {
				req_out->datastore = RC_DS_INTENDED;
			} else {
				req_out->datastore = RC_DS_UNKNOWN;
			}
			rest_path = "";
		}
	} else if (strcmp(path, "/restconf/operations") == 0 ||
	           strncmp(path, "/restconf/operations/", 21) == 0) {
		req_out->res_type = RC_RES_OPERATIONS;
		rest_path = (path[20] == '/') ? path + 21 : "";

		/* Parser le module:rpc-name depuis l'URI operations */
		if (rest_path && *rest_path != '\0') {
			char rpc_path[512];
			size_t rpc_len = strlen(rest_path);
			if (rpc_len >= sizeof(rpc_path)) return -1;
			memcpy(rpc_path, rest_path, rpc_len);
			rpc_path[rpc_len] = '\0';

			/* Enlever les trailing slashes */
			while (rpc_len > 0 &&
			       rpc_path[rpc_len - 1] == '/') {
				rpc_path[--rpc_len] = '\0';
			}

			/* Parser module:rpc ou module:action/node */
			char *colon = strchr(rpc_path, ':');
			if (colon) {
				*colon = '\0';
				req_out->rpc_module = strdup(rpc_path);
				req_out->rpc_name = strdup(colon + 1);
			} else {
				req_out->rpc_name = strdup(rpc_path);
			}
		}
	} else if (strncmp(path, "/restconf/stream/", 17) == 0 ||
	           strncmp(path, "/streams/", 9) == 0) {
		/* RFC 8040 Sec 6.3: Event Stream URIs */
		req_out->res_type = RC_RES_EVENT_STREAM;
		/* Le reste du path identifie le stream */
		if (strncmp(path, "/restconf/stream/", 17) == 0) {
			req_out->xpath = strdup(path + 17);
		} else {
			req_out->xpath = strdup(path + 9);
		}
	} else {
		req_out->res_type = RC_RES_UNKNOWN;
		return -1;
	}

	/* Parser les query parameters après extraction du path */
	if (query && parse_query_params(query, req_out) != 0) {
		return -1;
	}

	/* Construire le XPath pour DATA/DS uniquement */
	if ((req_out->res_type == RC_RES_DATA ||
	     req_out->res_type == RC_RES_DS) &&
	    rest_path && *rest_path != '\0') {
		char *xpath = malloc(MAX_XPATH_LEN);
		if (!xpath) return -1;
		xpath[0] = '\0';
		size_t xpath_len = 0;

		const char *seg_start = rest_path;
		while (*seg_start) {
			while (*seg_start == '/') seg_start++;
			if (*seg_start == '\0') break;

			const char *seg_end = strchr(seg_start, '/');
			size_t seg_len = seg_end ?
				(size_t)(seg_end - seg_start) :
				strlen(seg_start);

			/* Valider le percent-encoding du segment */
			if (validate_percent_encoding(seg_start, seg_len) != 0) {
				free(xpath);
				return -1;
			}

			if (parse_segment(ctx, xpath, &xpath_len,
			                  seg_start, seg_len) != 0) {
				free(xpath);
				return -1;
			}
			seg_start += seg_len;
		}
		req_out->xpath = xpath;
	}
	return 0;
}

void router_free_request(rc_request_t *req) {
	if (!req) return;
	free(req->xpath);
	free(req->rpc_module);
	free(req->rpc_name);
	free(req->content_filter);
	free(req->fields_expr);
	free(req->with_defaults);
	free(req->username);
	free(req->if_match);
}