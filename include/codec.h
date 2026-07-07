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

int codec_serialize_data_wd(
	const struct lyd_node *tree,
	media_type_t type,
	const char *with_defaults,
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

/**
 * @brief Filtre l'arbre lyd_node selon l'expression "fields".
 * RFC 8040 Sec 4.8.3 : syntaxe simplifiée.
 * @param tree Arbre à filtrer.
 * @param fields_expr Expression fields.
 * @param out_tree Pointeur vers l'arbre filtré.
 * @return 0 en cas de succès, -1 en cas d'erreur.
 */
int codec_filter_fields(
	const struct lyd_node *tree,
	const char *fields_expr,
	struct lyd_node **out_tree);

#endif /* CODEC_H */
