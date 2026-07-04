/*
 * Ce fichier cible l'API sysrepo "moderne" (sysrepo >= 2.x) dans laquelle
 * sr_get_data()/sr_get_subtree() renvoient un `sr_data_t *` (arbre
 * libyang + verrou de contexte), libere via sr_release_data(). Si votre
 * sysrepo installe est plus ancien (API 1.x, `struct lyd_node **` brut,
 * libere via sr_free_tree()/lyd_free_all()), adaptez les quelques lignes
 * reperees par le marqueur "XXX-SR-API" ci-dessous : c'est le seul
 * endroit qui depend de cette difference de version. Verifiez les
 * signatures exactes dans le sysrepo.h que vous avez installe (le depot
 * source est indique dans le fichier projet "sysrepo").
 *
 * De meme, la resolution des noms de cle de liste a partir du chemin
 * RESTCONF (qui ne transporte que les VALEURS de cle, dans l'ordre du
 * "key" YANG, cf. RFC 8040 SS3.5.3) s'appuie sur le parcours du schema
 * compile libyang (struct lysc_node, drapeau LYS_KEY). L'API publique de
 * parcours des enfants d'un noeud de schema a evolue entre les versions
 * de libyang 2.x ; verifiez lysc_node_children() dans votre
 * libyang/tree_schema.h si la compilation echoue a cet endroit
 * (marqueur "XXX-LY-API").
 */

#include "sysrepo_backend.h"
#include "http.h"

#include <libyang/libyang.h>
#include <sysrepo.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sr_conn_ctx_t *g_conn;

int sysrepo_backend_init(void)
{
    int rc = sr_connect(0, &g_conn);
    if (rc != SR_ERR_OK) {
        fprintf(stderr, "sr_connect: %s\n", sr_strerror(rc));
        return -1;
    }
    return 0;
}

void sysrepo_backend_destroy(void)
{
    if (g_conn) {
        sr_disconnect(g_conn);
        g_conn = NULL;
    }
}

struct ds_map_entry {
    const char *identityref;
    int sr_ds;
    int read_only;
};

/* RFC 8527 SS3.1 : les identityref sont qualifies par le module
 * "ietf-datastores". sysrepo (RFC 8342 NMDA) expose directement running/
 * candidate/startup/operational ; <intended> n'a pas d'equivalent direct
 * (c'est generalement le resultat de la validation de <running>, ce que
 * sysrepo ne materialise pas comme datastore separe) et n'est donc pas
 * propose ici. */
static const struct ds_map_entry DS_MAP[] = {
    { "ietf-datastores:running", SR_DS_RUNNING, 0 },
    { "ietf-datastores:candidate", SR_DS_CANDIDATE, 0 },
    { "ietf-datastores:startup", SR_DS_STARTUP, 0 },
    /* Ecriture desactivee par defaut sur <operational> : dans sysrepo,
     * cette datastore se peuple via les abonnements "push/oper get"
     * (sr_oper_get_items_subscribe, sr_set_item avec session operational
     * pour les "push oper data"), pas via des edits RESTCONF classiques.
     * A ajuster selon vos besoins (cf. RFC 8527 SS3.2.1 pour la
     * semantique with-defaults specifique a ce datastore). */
    { "ietf-datastores:operational", SR_DS_OPERATIONAL, 1 },
};

int sysrepo_backend_datastore_from_identityref(const char *identityref, int *sr_ds_out,
                                                int *read_only)
{
    if (!identityref) {
        return -1;
    }
    for (size_t i = 0; i < sizeof(DS_MAP) / sizeof(DS_MAP[0]); i++) {
        if (strcmp(DS_MAP[i].identityref, identityref) == 0) {
            if (sr_ds_out) {
                *sr_ds_out = DS_MAP[i].sr_ds;
            }
            if (read_only) {
                *read_only = DS_MAP[i].read_only;
            }
            return 0;
        }
    }
    return -1;
}

/* Ajoute 'suffix' a *buf (chaine C dynamique), en reallouant. */
static int str_append(char **buf, size_t *len, size_t *cap, const char *suffix)
{
    size_t slen = strlen(suffix);
    if (*len + slen + 1 > *cap) {
        size_t ncap = (*cap == 0) ? 64 : *cap;
        while (ncap < *len + slen + 1) {
            ncap *= 2;
        }
        char *nb = realloc(*buf, ncap);
        if (!nb) {
            return -1;
        }
        *buf = nb;
        *cap = ncap;
    }
    memcpy(*buf + *len, suffix, slen + 1);
    *len += slen;
    return 0;
}

/* Ajoute un predicat XPath "[name='value']" (ou avec des guillemets
 * doubles si 'value' contient un apostrophe) a *buf. */
static int append_predicate(char **buf, size_t *len, size_t *cap, const char *key_name,
                             const char *value, int leaflist)
{
    char quote = (strchr(value, '\'') != NULL) ? '"' : '\'';
    char pred[512];
    if (leaflist) {
        snprintf(pred, sizeof(pred), "[.=%c%s%c]", quote, value, quote);
    } else {
        snprintf(pred, sizeof(pred), "[%s=%c%s%c]", key_name, quote, value, quote);
    }
    return str_append(buf, len, cap, pred);
}

/* Construit le chemin sysrepo/libyang (XPath) correspondant a une suite
 * de segments RESTCONF, en resolvant les noms de cle de liste via le
 * schema compile. Retourne 0 et remplit *xpath_out (alloue) en cas de
 * succes ; -1 + *err en cas d'echec (segment inconnu du schema, nombre
 * de cles incoherent, etc. -> "invalid-value"). */
static int build_xpath(const struct ly_ctx *ctx, const struct restconf_path_segment *segments,
                        size_t nsegments, char **xpath_out, struct restconf_error *err)
{
    char *schema_path = NULL, *data_path = NULL;
    size_t schema_len = 0, schema_cap = 0;
    size_t data_len = 0, data_cap = 0;
    const char *current_module = NULL;

    for (size_t i = 0; i < nsegments; i++) {
        const struct restconf_path_segment *seg = &segments[i];
        const char *module = seg->module ? seg->module : current_module;
        if (!module) {
            err->error_type = strdup("protocol");
            err->error_tag = strdup("invalid-value");
            err->error_message = strdup("le premier segment du chemin doit indiquer un module");
            goto fail;
        }
        current_module = module;

        char seg_str[512];
        snprintf(seg_str, sizeof(seg_str), "/%s:%s", module, seg->name);
        if (str_append(&schema_path, &schema_len, &schema_cap, seg_str) != 0 ||
            str_append(&data_path, &data_len, &data_cap, seg_str) != 0) {
            goto oom;
        }

        /* XXX-LY-API : lys_find_path() resout un noeud de SCHEMA (donc
         * sans predicats de cle) a partir d'un chemin absolu. */
        const struct lysc_node *snode = lys_find_path(ctx, NULL, schema_path, 0);
        if (!snode) {
            err->error_type = strdup("protocol");
            err->error_tag = strdup("invalid-value");
            err->error_message = strdup("chemin RESTCONF sans correspondance dans le schema YANG charge");
            goto fail;
        }

        if (seg->nkeys > 0) {
            if (snode->nodetype == LYS_LEAFLIST) {
                if (seg->nkeys != 1) {
                    err->error_type = strdup("protocol");
                    err->error_tag = strdup("invalid-value");
                    err->error_message = strdup("une leaf-list n'accepte qu'une seule valeur");
                    goto fail;
                }
                if (append_predicate(&data_path, &data_len, &data_cap, NULL, seg->keys[0], 1) != 0) {
                    goto oom;
                }
            } else if (snode->nodetype == LYS_LIST) {
                /* XXX-LY-API : parcours des enfants du noeud de schema
                 * pour retrouver, dans l'ordre, les feuilles marquees
                 * LYS_KEY (ordre du "key" YANG == ordre attendu par la
                 * grammaire RESTCONF list-instance, RFC 8040 SS3.5.3). */
                size_t kidx = 0;
                const struct lysc_node *child = lysc_node_children(snode, 0);
                for (; child && kidx < seg->nkeys; child = child->next) {
                    if (child->nodetype == LYS_LEAF && (child->flags & LYS_KEY)) {
                        if (append_predicate(&data_path, &data_len, &data_cap, child->name,
                                              seg->keys[kidx], 0) != 0) {
                            goto oom;
                        }
                        kidx++;
                    }
                }
                if (kidx != seg->nkeys) {
                    err->error_type = strdup("protocol");
                    err->error_tag = strdup("invalid-value");
                    err->error_message =
                        strdup("nombre de valeurs de cle incoherent avec le schema de la liste");
                    goto fail;
                }
            } else {
                err->error_type = strdup("protocol");
                err->error_tag = strdup("invalid-value");
                err->error_message = strdup("des valeurs de cle ont ete fournies pour un noeud "
                                             "qui n'est ni une liste ni une leaf-list");
                goto fail;
            }
        }
    }

    free(schema_path);
    *xpath_out = data_path;
    return 0;

oom:
    free(schema_path);
    free(data_path);
    return -1;
fail:
    free(schema_path);
    free(data_path);
    return -1;
}

/* XXX-SR-API : point unique d'adaptation si votre sysrepo n'utilise pas
 * encore sr_data_t. Sous sysrepo 1.x, remplacez sr_data_t par
 * `struct lyd_node *` directement renvoye par sr_get_data()/
 * sr_get_subtree(), et sr_release_data(x) par lyd_free_all(x). */
struct fetch_result {
    sr_data_t *sr_data;
    struct lyd_node *tree; /* raccourci vers sr_data->tree, ne pas liberer separement */
};

static int fetch_tree(sr_session_ctx_t *session, const char *xpath, int whole_datastore,
                       const struct restconf_get_options *options, int sr_ds, struct fetch_result *out,
                       struct restconf_error *err)
{
    memset(out, 0, sizeof(*out));
    int rc;
    unsigned int depth = options ? options->depth : 0;
    enum restconf_content_mode content = options ? options->content : RESTCONF_CONTENT_ALL;

    if (whole_datastore || depth > 0) {
        uint32_t opts = 0;
        /* Le filtrage config/nonconfig via des drapeaux sr_get_data
         * n'a de sens que pour la datastore <operational> chez sysrepo
         * (running/candidate/startup ne contiennent que de la
         * configuration). Adaptez SR_OPER_NO_STATE/SR_OPER_NO_CONFIG a
         * l'enum sr_get_oper_flag_t reellement disponible chez vous. */
        if (sr_ds == SR_DS_OPERATIONAL) {
            if (content == RESTCONF_CONTENT_CONFIG) {
                opts |= SR_OPER_NO_STATE;
            } else if (content == RESTCONF_CONTENT_NONCONFIG) {
                opts |= SR_OPER_NO_CONFIG;
            }
        }
        rc = sr_get_data(session, xpath, depth, 0, opts, &out->sr_data);
    } else {
        rc = sr_get_subtree(session, xpath, 0, &out->sr_data);
    }

    if (rc != SR_ERR_OK) {
        err->error_type = strdup("application");
        err->error_tag = strdup(rc == SR_ERR_NOT_FOUND ? "invalid-value" : "operation-failed");
        err->error_message = strdup(sr_strerror(rc));
        return -1;
    }

    out->tree = out->sr_data ? out->sr_data->tree : NULL;
    return 0;
}

static void fetch_result_release(struct fetch_result *r)
{
    if (r->sr_data) {
        sr_release_data(r->sr_data);
    }
    memset(r, 0, sizeof(*r));
}

int sysrepo_backend_get(int sr_ds, const struct restconf_path_segment *segments,
                         size_t nsegments, const struct restconf_get_options *options,
                         char **json_out, struct restconf_error *err)
{
    *json_out = NULL;
    memset(err, 0, sizeof(*err));

    sr_session_ctx_t *session = NULL;
    int rc = sr_session_start(g_conn, (sr_datastore_t)sr_ds, &session);
    if (rc != SR_ERR_OK) {
        err->error_type = strdup("application");
        err->error_tag = strdup("operation-failed");
        err->error_message = strdup(sr_strerror(rc));
        return -1;
    }

    const struct ly_ctx *ctx = sr_acquire_context(g_conn);
    if (!ctx) {
        sr_session_stop(session);
        err->error_type = strdup("application");
        err->error_tag = strdup("operation-failed");
        err->error_message = strdup("contexte libyang indisponible");
        return -1;
    }

    char *xpath = NULL;
    int whole = (nsegments == 0);
    if (whole) {
        xpath = strdup("/*");
    } else if (build_xpath(ctx, segments, nsegments, &xpath, err) != 0) {
        sr_release_context(g_conn);
        sr_session_stop(session);
        return -1;
    }

    struct fetch_result fr;
    int frc = fetch_tree(session, xpath, whole, options, sr_ds, &fr, err);
    free(xpath);
    sr_release_context(g_conn);
    sr_session_stop(session);

    if (frc != 0) {
        return -1;
    }

    char *raw = NULL;
    if (fr.tree) {
        if (lyd_print_mem(&raw, fr.tree, LYD_JSON, LYD_PRINT_WITHSIBLINGS | LYD_PRINT_SHRINK) != 0) {
            fetch_result_release(&fr);
            err->error_type = strdup("application");
            err->error_tag = strdup("operation-failed");
            err->error_message = strdup("echec de serialisation JSON (lyd_print_mem)");
            return -1;
        }
    }
    fetch_result_release(&fr);

    if (!raw) {
        if (!whole) {
            /* Ressource ciblee absente : RFC 8040 SS4.3 -> 404 / invalid-value. */
            err->error_type = strdup("protocol");
            err->error_tag = strdup("invalid-value");
            err->error_message = strdup("la ressource demandee n'existe pas");
            return -1;
        }
        raw = strdup("{}");
    }

    if (whole) {
        /* {+restconf}/data (ou {+restconf}/ds/<name>) sans sous-chemin :
         * enveloppe dans le noeud racine "data" (RFC 8040 SS3.3.1 /
         * exemple SS3.5.3.1 tel qu'illustre par B.3.3). */
        size_t need = strlen(raw) + 64;
        char *wrapped = malloc(need);
        if (!wrapped) {
            free(raw);
            return -1;
        }
        snprintf(wrapped, need, "{\"ietf-restconf:data\":%s}", raw);
        free(raw);
        *json_out = wrapped;
    } else {
        /* libyang imprime deja le noeud cible sous la forme
         * {"module:nom": ...}, qui est exactement la representation
         * attendue pour une ressource de donnees RESTCONF. */
        *json_out = raw;
    }

    return 0;
}

int sysrepo_backend_default_data_datastore(void)
{
    return SR_DS_RUNNING;
}

/* --------------------------------------------------------------------
 * Ecritures (POST/PUT/PATCH/DELETE) -- RFC 8040 SS4.4/4.5/4.6.1/4.7
 * -------------------------------------------------------------------- */

/* Construit le segment de chemin RESTCONF ('module:nom', ou
 * 'module:nom=cle1,cle2' pour une instance de liste, ou 'module:nom=val'
 * pour une leaf-list) correspondant a un noeud de donnees fraichement
 * cree. Utilise pour l'en-tete 'Location' d'une reponse 201 (POST, RFC
 * 8040 SS4.4.1). Les valeurs de cle usuelles (identifiants) ne
 * necessitent pas de percent-encodage ; l'appelant HTTP se charge malgre
 * tout d'encoder le resultat par prudence (cf. restconf_handler.c). */
static char *describe_child_segment(const struct lyd_node *node)
{
    if (!node || !node->schema) {
        return NULL;
    }
    const struct lysc_node *snode = node->schema;
    char *buf = NULL;
    size_t len = 0, cap = 0;
    char head[512];
    snprintf(head, sizeof(head), "%s:%s", snode->module->name, snode->name);
    if (str_append(&buf, &len, &cap, head) != 0) {
        return NULL;
    }

    if (snode->nodetype == LYS_LIST) {
        int first = 1;
        /* XXX-LY-API : meme convention que build_xpath() pour retrouver,
         * dans l'ordre du schema, les feuilles de cle d'une instance de
         * liste fraichement analysee depuis le corps JSON. */
        for (const struct lyd_node *child = lyd_child(node); child; child = child->next) {
            if (child->schema && child->schema->nodetype == LYS_LEAF &&
                (child->schema->flags & LYS_KEY)) {
                const char *v = lyd_get_value(child);
                if (str_append(&buf, &len, &cap, first ? "=" : ",") != 0 ||
                    str_append(&buf, &len, &cap, v ? v : "") != 0) {
                    free(buf);
                    return NULL;
                }
                first = 0;
            }
        }
    } else if (snode->nodetype == LYS_LEAFLIST) {
        const char *v = lyd_get_value(node);
        if (str_append(&buf, &len, &cap, "=") != 0 || str_append(&buf, &len, &cap, v ? v : "") != 0) {
            free(buf);
            return NULL;
        }
    }

    return buf;
}

/* Traduit un code d'erreur sysrepo en error-tag RESTCONF pour les
 * operations d'ecriture (RFC 8040 SS7, tableau "Mapping from <error-tag>
 * to Status Code"). */
static const char *write_error_tag(int sr_rc)
{
    switch (sr_rc) {
    case SR_ERR_EXISTS:
        return "data-exists";
    case SR_ERR_NOT_FOUND:
        return "data-missing";
    case SR_ERR_LOCKED:
        return "lock-denied";
    case SR_ERR_UNAUTHORIZED:
        return "access-denied";
    case SR_ERR_VALIDATION_FAILED:
        return "invalid-value";
    default:
        return "operation-failed";
    }
}

int sysrepo_backend_write(int sr_ds, const struct restconf_path_segment *segments,
                          size_t nsegments, enum restconf_write_op op, const char *body_json,
                          size_t body_len, int *created_out, char **created_child_segment_out,
                          struct restconf_error *err)
{
    memset(err, 0, sizeof(*err));
    if (created_out) {
        *created_out = 0;
    }
    if (created_child_segment_out) {
        *created_child_segment_out = NULL;
    }

    if (op != RESTCONF_WRITE_CREATE && nsegments == 0) {
        /* PUT/PATCH sur la racine de la datastore ({+restconf}/data ou
         * {+restconf}/ds/<name> eux-memes) : le remplacement complet de
         * la datastore (RFC 8040 SS4.5, exemple Appendix B.2.4, avec son
         * enveloppe 'data') n'est pas gere par ce squelette. */
        err->error_type = strdup("application");
        err->error_tag = strdup("operation-not-supported");
        err->error_message =
            strdup("remplacement/fusion de la datastore entiere via PUT/PATCH sur la racine "
                   "non implemente dans ce squelette (cf. feuille de route)");
        return -1;
    }

    if (!body_json || body_len == 0) {
        err->error_type = strdup("protocol");
        err->error_tag = strdup("malformed-message");
        err->error_message = strdup("corps de requete JSON attendu et absent");
        return -1;
    }

    sr_session_ctx_t *session = NULL;
    int rc = sr_session_start(g_conn, (sr_datastore_t)sr_ds, &session);
    if (rc != SR_ERR_OK) {
        err->error_type = strdup("application");
        err->error_tag = strdup("operation-failed");
        err->error_message = strdup(sr_strerror(rc));
        return -1;
    }

    const struct ly_ctx *ctx = sr_acquire_context(g_conn);
    if (!ctx) {
        sr_session_stop(session);
        err->error_type = strdup("application");
        err->error_tag = strdup("operation-failed");
        err->error_message = strdup("contexte libyang indisponible");
        return -1;
    }

    /* Pour PUT, determine si la ressource CIBLE existe deja, afin de
     * distinguer 201 Created de 204 No Content (RFC 8040 SS4.5). */
    if (op == RESTCONF_WRITE_REPLACE && created_out && nsegments > 0) {
        char *target_xpath = NULL;
        struct restconf_error xerr;
        memset(&xerr, 0, sizeof(xerr));
        if (build_xpath(ctx, segments, nsegments, &target_xpath, &xerr) == 0) {
            sr_data_t *probe = NULL;
            int prc = sr_get_subtree(session, target_xpath, 0, &probe);
            *created_out = (prc == SR_ERR_NOT_FOUND) ? 1 : 0;
            if (probe) {
                sr_release_data(probe);
            }
            free(target_xpath);
        }
        restconf_error_release(&xerr);
    }

    /* Segments 'parents' sous lesquels le corps JSON doit s'inserer :
     * - POST : la totalite des segments de l'URI (c'est la ressource
     *   PARENTE au sens RFC 8040 SS4.4.1) ;
     * - PUT/PATCH : tous les segments sauf le dernier (le dernier est la
     *   ressource CIBLE elle-meme, dont le corps porte la representation). */
    size_t parent_n = (op == RESTCONF_WRITE_CREATE) ? nsegments : (nsegments > 0 ? nsegments - 1 : 0);

    struct lyd_node *top = NULL;          /* racine de l'arbre soumis a sr_edit_batch */
    struct lyd_node *attach_point = NULL; /* noeud sous lequel accrocher le corps JSON, ou NULL
                                            * si le corps est lui-meme un arbre de haut niveau */

    if (parent_n > 0) {
        char *parent_xpath = NULL;
        if (build_xpath(ctx, segments, parent_n, &parent_xpath, err) != 0) {
            sr_release_context(g_conn);
            sr_session_stop(session);
            return -1;
        }

        if (op == RESTCONF_WRITE_MERGE) {
            /* PATCH (plain patch) : le parent DOIT deja exister (sinon
             * 'data-missing', cf. tableau RFC 8040 SS7 -- un merge sur un
             * parent absent ne peut pas etre satisfait sans le creer
             * implicitement, ce que la semantique 'merge' n'autorise pas
             * ici par prudence). */
            sr_data_t *probe = NULL;
            int prc = sr_get_subtree(session, parent_xpath, 0, &probe);
            if (probe) {
                sr_release_data(probe);
            }
            if (prc == SR_ERR_NOT_FOUND) {
                free(parent_xpath);
                sr_release_context(g_conn);
                sr_session_stop(session);
                err->error_type = strdup("protocol");
                err->error_tag = strdup("data-missing");
                err->error_message = strdup("la ressource parente n'existe pas");
                return -1;
            }
        }

        /* XXX-LY-API : lyd_new_path2() cree (ou retrouve) le squelette
         * d'ancetres jusqu'a la ressource parente ; 'top' est le noeud le
         * plus haut cree (a soumettre tel quel a sr_edit_batch). D'apres
         * les discussions upstream libyang (CESNET/libyang#2337), le
         * 'new_node' renvoye pour un chemin cible une liste n'est pas
         * garanti etre l'entree de liste elle-meme selon les versions ;
         * on re-resout donc systematiquement 'attach_point' via
         * lyd_find_path() sur l'arbre obtenu, par securite. */
        struct lyd_node *new_node = NULL;
        LY_ERR lyrc = lyd_new_path2(NULL, ctx, parent_xpath, NULL, 0, 0, 0, &top, &new_node);
        if (lyrc != LY_SUCCESS || !top) {
            free(parent_xpath);
            sr_release_context(g_conn);
            sr_session_stop(session);
            err->error_type = strdup("protocol");
            err->error_tag = strdup("invalid-value");
            err->error_message = strdup("impossible de construire le chemin de la ressource "
                                         "parente (incoherent avec le schema YANG charge)");
            return -1;
        }
        if (lyd_find_path(top, parent_xpath, 0, &attach_point) != LY_SUCCESS || !attach_point) {
            attach_point = new_node; /* repli sur la valeur renvoyee directement */
        }
        free(parent_xpath);
    }

    /* Analyse le corps JSON (RFC 7951) comme enfant(s) de 'attach_point'
     * (ou comme arbre racine si 'attach_point' est NULL, c.-a-d. pour un
     * POST directement sur {+restconf}/data ou {+restconf}/ds/<name>). */
    struct ly_in *in = NULL;
    if (ly_in_new_memory(body_json, &in) != LY_SUCCESS) {
        if (top) {
            lyd_free_all(top);
        }
        sr_release_context(g_conn);
        sr_session_stop(session);
        err->error_type = strdup("application");
        err->error_tag = strdup("operation-failed");
        err->error_message = strdup("echec d'allocation pour l'analyse du corps JSON");
        return -1;
    }

    struct lyd_node *parsed_tree = NULL;
    LY_ERR lyrc = lyd_parse_data(ctx, attach_point, in, LYD_JSON, LYD_PARSE_ONLY | LYD_PARSE_NO_STATE,
                                  0, &parsed_tree);
    ly_in_free(in, 0);

    if (lyrc != LY_SUCCESS) {
        if (top) {
            lyd_free_all(top);
        } else if (parsed_tree) {
            lyd_free_all(parsed_tree);
        }
        sr_release_context(g_conn);
        sr_session_stop(session);
        err->error_type = strdup("protocol");
        err->error_tag = strdup("malformed-message");
        err->error_message = strdup("corps de requete JSON non conforme au schema YANG charge "
                                     "(RFC 7951) pour cet emplacement");
        return -1;
    }

    if (!attach_point) {
        /* {+restconf}/data ou {+restconf}/ds/<name> eux-memes : le corps
         * EST l'arbre de haut niveau a soumettre. */
        top = parsed_tree;
    }
    if (!top) {
        /* Corps JSON vide ('{}') : rien a faire (cf. exemple RFC 8040
         * SS4.4.1, creation d'un conteneur vide). */
        sr_release_context(g_conn);
        sr_session_stop(session);
        return 0;
    }

    /* Tague les noeuds apportes par le corps JSON (enfants directs du
     * point d'attache, ou racine(s) de l'arbre s'il n'y avait pas de
     * point d'attache) avec l'operation d'edition NETCONF correspondante :
     *  - POST  -> 'create'  (SR_ERR_EXISTS -> 409 data-exists si deja present)
     *  - PUT   -> 'replace' (remplace tout le sous-arbre existant)
     *  - PATCH -> 'merge'   (fusion, 'plain patch' RFC 8040 SS4.6.1)
     * Le squelette d'ancetres (le cas echeant) n'est lui jamais tague :
     * il reste soumis a l'operation par defaut 'merge' de sr_edit_batch(),
     * sans effet de bord s'il correspond a des donnees deja existantes. */
    const char *op_attr = (op == RESTCONF_WRITE_CREATE)
                              ? "create"
                              : (op == RESTCONF_WRITE_REPLACE ? "replace" : "merge");
    struct lyd_node *tag_start = attach_point ? lyd_child(attach_point) : top;

    if (op == RESTCONF_WRITE_CREATE && created_child_segment_out && tag_start) {
        *created_child_segment_out = describe_child_segment(tag_start);
    }

    for (struct lyd_node *n = tag_start; n; n = n->next) {
        if (lyd_new_attr(n, "ietf-netconf", "ietf-netconf:operation", op_attr, NULL) != LY_SUCCESS) {
            lyd_free_all(top);
            sr_release_context(g_conn);
            sr_session_stop(session);
            err->error_type = strdup("application");
            err->error_tag = strdup("operation-failed");
            err->error_message = strdup("echec de marquage de l'operation d'edition (metadata "
                                         "ietf-netconf:operation)");
            return -1;
        }
    }

    rc = sr_edit_batch(session, top, "merge");
    if (rc == SR_ERR_OK) {
        rc = sr_apply_changes(session, 0);
    }

    lyd_free_all(top);
    sr_release_context(g_conn);
    sr_session_stop(session);

    if (rc != SR_ERR_OK) {
        if (created_child_segment_out && *created_child_segment_out) {
            free(*created_child_segment_out);
            *created_child_segment_out = NULL;
        }
        err->error_type = strdup("application");
        err->error_tag = strdup(write_error_tag(rc));
        err->error_message = strdup(sr_strerror(rc));
        return -1;
    }

    return 0;
}

int sysrepo_backend_delete(int sr_ds, const struct restconf_path_segment *segments,
                           size_t nsegments, struct restconf_error *err)
{
    memset(err, 0, sizeof(*err));

    if (nsegments == 0) {
        /* RFC 8040 SS4.7 : DELETE cible une ressource de donnees, pas la
         * datastore/racine {+restconf}/data elle-meme. */
        err->error_type = strdup("protocol");
        err->error_tag = strdup("operation-not-supported");
        err->error_message = strdup("DELETE n'est pas defini sur la racine d'une datastore");
        return -1;
    }

    sr_session_ctx_t *session = NULL;
    int rc = sr_session_start(g_conn, (sr_datastore_t)sr_ds, &session);
    if (rc != SR_ERR_OK) {
        err->error_type = strdup("application");
        err->error_tag = strdup("operation-failed");
        err->error_message = strdup(sr_strerror(rc));
        return -1;
    }

    const struct ly_ctx *ctx = sr_acquire_context(g_conn);
    if (!ctx) {
        sr_session_stop(session);
        err->error_type = strdup("application");
        err->error_tag = strdup("operation-failed");
        err->error_message = strdup("contexte libyang indisponible");
        return -1;
    }

    char *xpath = NULL;
    if (build_xpath(ctx, segments, nsegments, &xpath, err) != 0) {
        sr_release_context(g_conn);
        sr_session_stop(session);
        return -1;
    }

    /* SR_EDIT_STRICT : la ressource DOIT deja exister (RFC 8040 SS4.7 :
     * sinon 404 invalid-value, via SR_ERR_NOT_FOUND ci-dessous). */
    rc = sr_delete_item(session, xpath, SR_EDIT_STRICT);
    if (rc == SR_ERR_OK) {
        rc = sr_apply_changes(session, 0);
    }
    free(xpath);
    sr_release_context(g_conn);
    sr_session_stop(session);

    if (rc != SR_ERR_OK) {
        err->error_type = strdup("application");
        err->error_tag = strdup(rc == SR_ERR_NOT_FOUND ? "invalid-value" : write_error_tag(rc));
        err->error_message = strdup(sr_strerror(rc));
        return -1;
    }

    return 0;
}

int sysrepo_backend_get_yang_library_revision(char **revision_out, struct restconf_error *err)
{
    *revision_out = NULL;
    const struct ly_ctx *ctx = sr_acquire_context(g_conn);
    if (!ctx) {
        err->error_type = strdup("application");
        err->error_tag = strdup("operation-failed");
        err->error_message = strdup("contexte libyang indisponible");
        return -1;
    }

    const struct lys_module *mod = ly_ctx_get_module_implemented(ctx, "ietf-yang-library");
    const char *revision = (mod && mod->revision) ? mod->revision : "unknown";
    char *dup = strdup(revision);
    sr_release_context(g_conn);

    if (!dup) {
        err->error_type = strdup("application");
        err->error_tag = strdup("operation-failed");
        return -1;
    }
    *revision_out = dup;
    return 0;
}

int sysrepo_backend_yang_library_version(char **json_out, struct restconf_error *err)
{
    *json_out = NULL;
    char *revision = NULL;
    if (sysrepo_backend_get_yang_library_revision(&revision, err) != 0) {
        return -1;
    }

    size_t need = strlen(revision) + 48;
    char *buf = malloc(need);
    if (!buf) {
        free(revision);
        err->error_type = strdup("application");
        err->error_tag = strdup("operation-failed");
        return -1;
    }
    snprintf(buf, need, "{\"ietf-restconf:yang-library-version\":\"%s\"}", revision);
    free(revision);
    *json_out = buf;
    return 0;
}
