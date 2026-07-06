#define _GNU_SOURCE
#define UNUSED __attribute__((unused))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "router.h"

int router_parse_request(
	const char *path, const char *method,
	const char *auth_header UNUSED,
	const char *content_type,
	const char *accept,
	rc_request_t *req_out)
{
	if (!path || !method || !req_out) {
		return -1;
	}
	memset(req_out, 0, sizeof(rc_request_t));
	req_out->method = method;
	req_out->depth = -1;

	/* Parsing des médias */
	req_out->req_type = codec_parse_content_type(
		content_type);
	req_out->accept_type = codec_parse_accept(accept);

	if (strcmp(path, "/.well-known/host-meta") == 0) {
		req_out->res_type = RC_RES_ROOT_DISCOVERY;
		return 0;
	}

	if (strcmp(path, "/restconf") == 0) {
		req_out->res_type = RC_RES_API;
		return 0;
	}

	if (strncmp(path, "/restconf/data/", 15) == 0) {
		req_out->res_type = RC_RES_DATA;
		req_out->xpath = strdup(path + 14);
		return 0;
	}

	if (strncmp(path, "/restconf/ds/", 13) == 0) {
		req_out->res_type = RC_RES_DS;
		return 0;
	}

	if (strncmp(path, "/restconf/operations/", 21) == 0) {
		req_out->res_type = RC_RES_OPERATIONS;
		return 0;
	}

	req_out->res_type = RC_RES_UNKNOWN;
	return -1;
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