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
 * @brief Décode une chaîne percent-encoded (RFC 3986).
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
				dst[j++] = src[i++];
			}
		} else {
			dst[j++] = src[i++];
		}
	}
	dst[j] = '\0';
	return 0;
}

/**
 * @brief Construit le prédicat XPath pour une liste ou leaf-list.
 * Utilise le schéma YANG pour trouver les noms des clés.
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
		/* Fallback si le schéma n'est pas chargé */
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
		return -1; /* Erreur : '=' sur un nœud non list/leaf-list */
	}
	return 0;
}

/**
 * @brief Parse un segment d'URI et l'ajoute au XPath.
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

	if (strcmp(path, "/.well-known/host-meta") == 0) {
		req_out->res_type = RC_RES_ROOT_DISCOVERY;
		return 0;
	}
	if (strcmp(path, "/restconf") == 0) {
		req_out->res_type = RC_RES_API;
		return 0;
	}

	const char *rest_path = NULL;
	if (strncmp(path, "/restconf/data/", 15) == 0) {
		req_out->res_type = RC_RES_DATA;
		rest_path = path + 15;
	} else if (strncmp(path, "/restconf/ds/", 13) == 0) {
		req_out->res_type = RC_RES_DS;
		const char *ds_start = path + 13;
		const char *ds_end = strchr(ds_start, '/');
		if (ds_end) {
			/* TODO: Mapper l'identityref au datastore sysrepo */
			rest_path = ds_end + 1;
		} else {
			return -1;
		}
	} else if (strncmp(path, "/restconf/operations/", 21) == 0) {
		req_out->res_type = RC_RES_OPERATIONS;
		rest_path = path + 21;
	} else {
		req_out->res_type = RC_RES_UNKNOWN;
		return -1;
	}

	if (rest_path && *rest_path != '\0') {
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
	free(req->with_defaults);
	free(req->username);
}