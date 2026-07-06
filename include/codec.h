#ifndef CODEC_H
#define CODEC_H

#include <stdint.h>
#include <stddef.h>
#include <sysrepo.h>
#include <libyang/libyang.h>

typedef enum {
	MEDIA_TYPE_JSON,
	MEDIA_TYPE_XML,
	MEDIA_TYPE_UNKNOWN
} media_type_t;

media_type_t codec_parse_content_type(const char *header);
media_type_t codec_parse_accept(const char *header);

int codec_serialize_data(
	const struct lyd_node *tree,
	media_type_t type,
	char **out_buf,
	size_t *out_len);

int codec_parse_data(
	sr_session_ctx_t *session,
	const char *payload,
	size_t len,
	media_type_t type,
	struct lyd_node **tree);

int codec_serialize_errors(
	media_type_t type,
	const char *error_tag,
	const char *error_msg,
	char **out_buf,
	size_t *out_len);

#endif /* CODEC_H */