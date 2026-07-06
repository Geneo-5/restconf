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
	if (!tree || !out_buf || !out_len) return -1;

	LYD_FORMAT ly_fmt = (type == MEDIA_TYPE_XML) ?
		LYD_XML : LYD_JSON;

	uint32_t options = LYD_PRINT_SIBLINGS | LYD_PRINT_SHRINK;

	if (lyd_print_mem(out_buf, tree, ly_fmt, options) != 0) {
		return -1;
	}

	*out_len = strlen(*out_buf);
	return 0;
}

int codec_parse_data(
	sr_session_ctx_t *session,
	const char *payload,
	media_type_t type,
	struct lyd_node **tree)
{
	if (!session || !payload || !tree) return -1;

	LYD_FORMAT ly_fmt = (type == MEDIA_TYPE_XML) ?
		LYD_XML : LYD_JSON;
	
	/* Acquisition correcte du contexte libyang via sysrepo */
	const struct ly_ctx *ly_ctx = sr_acquire_context(
		sr_session_get_connection(session));
	if (!ly_ctx) return -1;

	uint32_t parse_opts = LYD_PARSE_STRICT | LYD_PARSE_OPAQ;
	uint32_t validate_opts = LYD_VALIDATE_PRESENT;

	if (lyd_parse_data_mem(
	        ly_ctx, payload, ly_fmt,
	        parse_opts, validate_opts, tree) != 0) {
		/* Libération obligatoire en cas d'erreur */
		sr_release_context(
			sr_session_get_connection(session));
		return -1;
	}
	
	/* Libération obligatoire après utilisation */
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