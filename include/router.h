#ifndef ROUTER_H
#define ROUTER_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <libyang/libyang.h>
#include "codec.h"

typedef enum {
	RC_RES_ROOT_DISCOVERY,
	RC_RES_ROOT_DISCOVERY_JSON,
	RC_RES_API,
	RC_RES_DATA,
	RC_RES_DS,
	RC_RES_OPERATIONS,
	RC_RES_EVENT_STREAM,
	/* RFC 8527 Sec 5.2: commit candidate to running */
	RC_RES_COMMIT,
	/* RFC 8527 Sec 5.3: discard candidate changes */
	RC_RES_DISCARD,
	RC_RES_UNKNOWN
} rc_resource_type_t;

typedef enum {
	RC_DS_RUNNING,
	RC_DS_OPERATIONAL,
	RC_DS_INTENDED,
	RC_DS_CANDIDATE,
	RC_DS_STARTUP,
	RC_DS_UNKNOWN
} rc_datastore_t;

typedef struct {
	const char *method;
	rc_resource_type_t res_type;
	rc_datastore_t datastore;
	/* true if a datastore identity was parsed from the URI
	 * (e.g. /restconf/ds/ietf-datastores:running), false if
	 * no datastore was specified (e.g. /restconf/ds). Used
	 * to distinguish "unknown datastore" (404) from "list
	 * datastores" (200). */
	bool ds_specified;
	char *xpath;
	char *rpc_module;
	char *rpc_name;
	bool with_origin;
	int depth;
	char *content_filter;
	char *fields_expr;
	char *with_defaults;
	const uint8_t *body;
	size_t body_len;
	char *username;
	media_type_t req_type;
	media_type_t accept_type;
	char *if_match;
	/* RFC 8040 Sec 4.8.7 / 6.3 (ROADMAP.md item 6.5) : bornes de
	 * replay pour un flux d'evenements (GET /streams/<name>
	 * ?start-time=...&stop-time=...), 0 si absent. Parsees depuis
	 * un timestamp RFC 3339 par parse_rfc3339() dans router.c. */
	time_t start_time;
	time_t stop_time;
} rc_request_t;

/**
 * @brief Parse l'URI RESTCONF et génère le XPath sysrepo.
 * @param ctx Contexte libyang (requis pour résoudre les clés de listes).
 */
int router_parse_request(
	const struct ly_ctx *ctx,
	const char *path, const char *method,
	const char *auth_header,
	const char *content_type,
	const char *accept,
	const char *if_match,
	rc_request_t *req_out);

void router_free_request(rc_request_t *req);

#endif /* ROUTER_H */