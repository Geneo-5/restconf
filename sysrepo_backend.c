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
                       enum restconf_content_mode content, int sr_ds, struct fetch_result *out,
                       struct restconf_error *err)
{
    memset(out, 0, sizeof(*out));
    int rc;

    if (whole_datastore) {
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
        rc = sr_get_data(session, xpath, 0, 0, opts, &out->sr_data);
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
                         size_t nsegments, enum restconf_content_mode content,
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
    int frc = fetch_tree(session, xpath, whole, content, sr_ds, &fr, err);
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
