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

/**
 * @brief Parse un corps de requête RESTCONF encodant l'input d'un
 * RPC ou d'une action (RFC 8040 Sec 3.6.1) directement dans un
 * nœud d'opération déjà créé.
 *
 * Contrairement à codec_parse_data() (basée sur
 * lyd_parse_data_mem(), destinée aux arbres de données de
 * datastore), cette fonction utilise lyd_parse_op() avec
 * #LYD_TYPE_RPC_RESTCONF : le corps reçu est l'objet JSON
 * "module:input" (ou l'élément XML <input>) tel que défini par
 * RFC 8040 Sec 3.6.1, PAS un nœud de données de premier niveau
 * générique — "input" n'a aucun sens comme ressource de données
 * autonome, donc lyd_parse_data_mem() ne peut pas le parser
 * correctement (cf. ROADMAP.md item 4.10).
 *
 * D'après la documentation libyang de lyd_parse_op(), pour
 * #LYD_TYPE_RPC_RESTCONF le paramètre "parent" DOIT pointer sur
 * le nœud RPC/action déjà créé (typiquement via lyd_new_path()) :
 * les enfants parsés depuis l'input sont rattachés directement
 * sous ce nœud, et le paramètre "op" de sortie doit rester NULL.
 *
 * @param[in] session Session sysrepo (utilisée pour acquérir le
 *            contexte libyang).
 * @param[in,out] op_node Nœud RPC/action nu déjà créé (ex. via
 *            lyd_new_path()) ; les nœuds enfants parsés depuis
 *            @p payload y sont rattachés directement.
 * @param[in] payload Corps brut de la requête RESTCONF.
 * @param[in] len Longueur de @p payload en octets.
 * @param[in] type Type de média (JSON ou XML) de @p payload.
 *
 * @return 0 en cas de succès, -1 en cas d'erreur.
 */
int codec_parse_rpc_input(
	sr_session_ctx_t *session,
	struct lyd_node *op_node,
	const char *payload,
	size_t len,
	media_type_t type);

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
