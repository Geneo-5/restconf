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
 */
static int build_xpath_predicate(
	const struct ly_ctx *ctx,
	const char *current_xpath,
	const char *module_name,
	const char *node_name,
	const char *values_str,
	char *out_buf, size_t *out_len)
{
	char path[2048];
	int written;

	if (module_name) {
		written = snprintf(path, sizeof(path), "%s/%s:%s",
		                   current_xpath, module_name,
		                   node_name);
	} else {
		written = snprintf(path, sizeof(path), "%s/%s",
		                   current_xpath, node_name);
	}
	if (written < 0 || (size_t)written >= sizeof(path)) {
		return -1;
	}

	const struct lysc_node *node = lys_find_path(
		ctx, NULL, path, 0);

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
		module_name = seg_buf;
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

		/* Ajouter le prédicat [key='val'] */
		if (ctx) {
			return build_xpath_predicate(
				ctx, current_xpath, module_name,
				node_name, values_str,
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
	rc_request_t *req_out)
{
	if (!path || !method || !req_out) return -1;

	memset(req_out, 0, sizeof(rc_request_t));
	req_out->method = method;
	req_out->depth = -1;
	req_out->req_type = codec_parse_content_type(content_type);
	req_out->accept_type = codec_parse_accept(accept);

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
	if (strncmp(path, "/restconf/data/", 15) == 0) {
		req_out->res_type = RC_RES_DATA;
		/* RFC 8040: /restconf/data cible running par défaut */
		req_out->datastore = RC_DS_RUNNING;
		rest_path = path + 15;
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
			return -1;
		}
	} else if (strncmp(path, "/restconf/operations/", 21) == 0) {
		req_out->res_type = RC_RES_OPERATIONS;
		rest_path = path + 21;

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
	} else if (strncmp(path, "/streams/", 9) == 0) {
		/* RFC 8040 Sec 6.3: Event Stream URIs */
		req_out->res_type = RC_RES_EVENT_STREAM;
		/* Le reste du path identifie le stream */
		req_out->xpath = strdup(path + 9);
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
}