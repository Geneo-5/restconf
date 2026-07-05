/*
 * _XOPEN_SOURCE (>= 500) est necessaire pour que <time.h> declare
 * strptime() (utilisee par parse_http_date() ci-dessous, RFC 7232 SS3.4)
 * -- ni le mode gnu11 par defaut de ce projet (cf. CMakeLists.txt) ni
 * _DEFAULT_SOURCE (implicite sous glibc en l'absence d'autre macro de
 * test) ne l'exposent seuls. _DEFAULT_SOURCE est redefini explicitement
 * juste apres pour conserver les extensions BSD/GNU deja utilisees par
 * ailleurs dans ce fichier (timegm(), strdup(), strndup(), gmtime_r()) :
 * definir _XOPEN_SOURCE seul desactiverait ces dernieres sous glibc. Ces
 * macros DOIVENT etre definies avant tout #include, y compris les
 * en-tetes systeme inclus transitivement par sysrepo_backend.h/http.h. */
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

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
 * compile libyang (struct lysc_node, drapeau LYS_KEY) via lysc_node_child()
 * (premier enfant, puis chainage via ->next) ; verifiee compiler contre
 * CESNET/libyang branche master le 2026-07-05 (marqueur "XXX-LY-API" aux
 * endroits sensibles si votre version differe).
 */

#include "sysrepo_backend.h"
#include "http.h"

#include <libyang/libyang.h>
#include <sysrepo.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>

static sr_conn_ctx_t *g_conn;

static const char *REQUIRED_YANG_MODULES[] = {
    "ietf-yang-library",
    "ietf-datastores",
    "ietf-restconf",
    "ietf-restconf-monitoring",
    "ietf-netconf",
};

static int validate_required_yang_modules(void)
{
    const struct ly_ctx *ctx = sr_acquire_context(g_conn);
    if (!ctx) {
        fprintf(stderr, "sr_acquire_context: contexte libyang indisponible\n");
        return -1;
    }

    int missing = 0;
    for (size_t i = 0; i < sizeof(REQUIRED_YANG_MODULES) / sizeof(REQUIRED_YANG_MODULES[0]); i++) {
        const char *name = REQUIRED_YANG_MODULES[i];
        if (!ly_ctx_get_module_implemented(ctx, name)) {
            fprintf(stderr, "module YANG requis absent de sysrepo/libyang: %s\n", name);
            missing = 1;
        }
    }

    sr_release_context(g_conn);

    if (missing) {
        fprintf(stderr, "installez les modules RESTCONF/NMDA requis avec sysrepoctl avant de "
                        "lancer restconfd\n");
        return -1;
    }
    return 0;
}

int sysrepo_backend_init(void)
{
    int rc = sr_connect(0, &g_conn);
    if (rc != SR_ERR_OK) {
        fprintf(stderr, "sr_connect: %s\n", sr_strerror(rc));
        return -1;
    }
    if (validate_required_yang_modules() != 0) {
        sr_disconnect(g_conn);
        g_conn = NULL;
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

/* --------------------------------------------------------------------
 * ETag / Last-Modified (RFC 8040 SS3.4.1 Edit Collision Prevention)
 * -------------------------------------------------------------------- */

/* Ce squelette ne maintient un timestamp/entity-tag QUE au niveau de
 * chaque datastore dans son ensemble (pas par ressource de donnees
 * individuelle) : c'est explicitement permis par la RFC 8040 (SS3.5.1
 * "If not maintained, then the resource timestamp for the datastore MUST
 * be used instead", SS3.5.2 idem pour l'entity-tag). L'etat est garde en
 * memoire process (pas persiste : redemarrer restconfd reinitialise
 * l'ETag/Last-Modified de chaque datastore a l'heure de demarrage), ce
 * qui est suffisant pour detecter des collisions entre clients RESTCONF
 * concurrents pendant la duree de vie du processus mais PAS a travers un
 * redemarrage, ni des modifications faites hors RESTCONF (CLI sysrepo,
 * NETCONF, etc.) -- limitation a documenter cote operateurs.
 *
 * Indexe directement par valeur de sr_datastore_t (running/candidate/
 * startup/operational tiennent toutes dans de petites valeurs entieres
 * chez sysrepo) ; REVISION_TABLE_SIZE est une borne large et defensive
 * plutot qu'une dependance a des constantes precises de sysrepo.h. */
#define REVISION_TABLE_SIZE 8

struct ds_revision {
    int initialized;
    time_t last_modified;
    unsigned long etag_counter;
};

static struct ds_revision g_revisions[REVISION_TABLE_SIZE];
static pthread_mutex_t g_revision_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct ds_revision *revision_slot(int sr_ds)
{
    if (sr_ds < 0 || sr_ds >= REVISION_TABLE_SIZE) {
        return NULL; /* datastore hors plage prevue : pas de suivi (defensif) */
    }
    return &g_revisions[sr_ds];
}

/* Initialise paresseusement (au premier acces) le slot d'une datastore
 * avec l'heure courante et le compteur 0 si ce n'est pas deja fait --
 * evite d'exiger un appel explicite depuis sysrepo_backend_init() pour
 * chaque valeur possible de sr_datastore_t. DOIT etre appelee avec
 * g_revision_mutex deja tenu par l'appelant. */
static void ensure_revision_initialized(struct ds_revision *slot)
{
    if (!slot->initialized) {
        slot->last_modified = time(NULL);
        slot->etag_counter = 0;
        slot->initialized = 1;
    }
}

/* A appeler apres toute ecriture reussie (POST/PUT/PATCH/DELETE, y compris
 * le remplacement/fusion complet de la datastore) sur une datastore de
 * CONFIGURATION. Avance l'ETag et met a jour Last-Modified a l'heure
 * courante (RFC 8040 SS3.4.1.1 Timestamp / SS3.4.1.2 Entity-Tag /
 * SS3.4.1.3 Update Procedure : "changes to configuration data resources
 * affect ... the datastore resource"). */
static void bump_datastore_revision(int sr_ds)
{
    struct ds_revision *slot = revision_slot(sr_ds);
    if (!slot) {
        return;
    }
    pthread_mutex_lock(&g_revision_mutex);
    ensure_revision_initialized(slot);
    slot->etag_counter++;
    slot->last_modified = time(NULL);
    pthread_mutex_unlock(&g_revision_mutex);
}

/* Formate 't' en HTTP-date (IMF-fixdate, RFC 7231 SS7.1.1.1), p.ex.
 * "Sun, 06 Nov 1994 08:49:37 GMT". Renvoie une chaine allouee, ou NULL en
 * cas d'echec d'allocation. */
static char *format_http_date(time_t t)
{
    struct tm tm_utc;
    gmtime_r(&t, &tm_utc);
    char *buf = malloc(32);
    if (!buf) {
        return NULL;
    }
    strftime(buf, 32, "%a, %d %b %Y %H:%M:%S GMT", &tm_utc);
    return buf;
}

void sysrepo_backend_get_datastore_revision(int sr_ds, char **etag_out, char **last_modified_out)
{
    if (etag_out) {
        *etag_out = NULL;
    }
    if (last_modified_out) {
        *last_modified_out = NULL;
    }
    struct ds_revision *slot = revision_slot(sr_ds);
    if (!slot) {
        return;
    }

    unsigned long etag_counter;
    time_t last_modified;
    pthread_mutex_lock(&g_revision_mutex);
    ensure_revision_initialized(slot);
    etag_counter = slot->etag_counter;
    last_modified = slot->last_modified;
    pthread_mutex_unlock(&g_revision_mutex);

    if (etag_out) {
        char buf[32];
        snprintf(buf, sizeof(buf), "\"%lu\"", etag_counter);
        *etag_out = strdup(buf);
    }
    if (last_modified_out) {
        *last_modified_out = format_http_date(last_modified);
    }
}

/* Analyse une chaine HTTP-date IMF-fixdate (RFC 7231 SS7.1.1.1, format
 * produit par format_http_date() ci-dessus et par la quasi-totalite des
 * clients HTTP modernes) en time_t UTC via strptime()/timegm() -- des
 * extensions BSD/GNU disponibles par defaut sous glibc en mode non-strict
 * (CMAKE_C_EXTENSIONS=ON, -std=gnu11 par defaut dans ce projet, cf.
 * CMakeLists.txt), comme strdup()/strndup() deja utilises ailleurs dans ce
 * fichier. Les formats obsoletes RFC 850/asctime (RFC 7231 SS7.1.1.1) ne
 * sont volontairement pas geres. Renvoie 0 en cas de succes, -1 si la
 * chaine ne correspond pas au format attendu. */
static int parse_http_date(const char *s, time_t *out)
{
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    if (!strptime(s, "%a, %d %b %Y %H:%M:%S GMT", &tm)) {
        return -1;
    }
    *out = timegm(&tm);
    return 0;
}

/* Verifie si la valeur d'ETag courante de sr_ds figure dans la liste
 * (separee par des virgules, RFC 7232 SS3.1) fournie par 'if_match'.
 * Traite aussi le cas particulier "*" (correspond a toute ressource
 * existante -- toujours vrai ici puisqu'une datastore existe toujours). */
static int if_match_satisfied(const char *if_match, const char *current_etag)
{
    if (strcmp(if_match, "*") == 0) {
        return 1;
    }
    size_t current_len = strlen(current_etag);
    const char *p = if_match;
    while (*p) {
        while (*p == ' ' || *p == ',') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - start) : strlen(start);
        while (len > 0 && start[len - 1] == ' ') {
            len--;
        }
        if (len == current_len && strncmp(start, current_etag, len) == 0) {
            return 1;
        }
        p = comma ? comma + 1 : start + len;
    }
    return 0;
}

int sysrepo_backend_check_preconditions(int sr_ds, const char *if_match,
                                        const char *if_unmodified_since)
{
    if (!if_match && !if_unmodified_since) {
        return 0;
    }

    char *etag = NULL;
    char *last_modified_str = NULL;
    sysrepo_backend_get_datastore_revision(sr_ds, &etag, &last_modified_str);

    int reject = 0;
    if (if_match && etag && !if_match_satisfied(if_match, etag)) {
        reject = 1;
    }
    if (!reject && if_unmodified_since) {
        time_t client_time;
        if (parse_http_date(if_unmodified_since, &client_time) == 0) {
            struct ds_revision *slot = revision_slot(sr_ds);
            if (slot) {
                pthread_mutex_lock(&g_revision_mutex);
                ensure_revision_initialized(slot);
                time_t current = slot->last_modified;
                pthread_mutex_unlock(&g_revision_mutex);
                if (current > client_time) {
                    reject = 1;
                }
            }
        }
        /* Date non parsable : ignoree (fail-open), RFC 7232 SS3.4. */
    }

    free(etag);
    free(last_modified_str);
    return reject;
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
                const struct lysc_node *child = lysc_node_child(snode);
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
    int with_origin = options ? options->with_origin : 0;

    /* RFC 8527 SS3.2.2 : "with-origin" necessite le drapeau sysrepo
     * SR_OPER_WITH_ORIGIN, qui n'est disponible que sur le chemin
     * sr_get_data() (sr_get_subtree() ne prend pas de drapeaux
     * sr_get_oper_flag_t) -- on force donc ce chemin des que with-origin
     * est demande, meme sans depth explicite. restconf_handler.c a deja
     * verifie que with-origin n'est utilise que sur la datastore
     * operational ; on le reverifie ici (sr_ds == SR_DS_OPERATIONAL) par
     * prudence supplementaire avant de poser le drapeau. */
    if (whole_datastore || depth > 0 || with_origin) {
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
            if (with_origin) {
                /* XXX-SR-API : nom exact du drapeau a verifier dans votre
                 * sysrepo.h (enum sr_get_oper_flag_t) ; il peut differer
                 * selon la version de sysrepo installee, comme
                 * SR_OPER_NO_STATE/SR_OPER_NO_CONFIG ci-dessus. */
                opts |= SR_OPER_WITH_ORIGIN;
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

/* --------------------------------------------------------------------
 * Parametre de requete "with-defaults" (RFC 8040 SS4.8.9)
 * -------------------------------------------------------------------- */

/* Traduit le mode "with-defaults" RESTCONF vers l'option d'impression
 * libyang correspondante (LYD_PRINT_WD_*), qui implemente directement la
 * meme semantique (RFC 6243 SS3.1-3.4, dont la RFC 8040 SS4.8.9 reprend
 * les renvois section par section). RESTCONF_WD_UNSET (parametre absent)
 * applique le basic-mode annonce par ce serveur dans la capacite
 * "defaults" (RFC 8040 SS9.1.2) -- ici "explicit", cf.
 * handle_restconf_monitoring_capabilities() dans restconf_handler.c.
 *
 * XXX-LY-API : verifiez ces noms de constantes dans votre
 * libyang/printer_data.h installe (LYD_PRINT_WD_EXPLICIT/TRIM/ALL/
 * ALL_TAG), comme pour les autres marqueurs XXX-LY-API de ce fichier. */
static int with_defaults_print_flag(enum restconf_with_defaults_mode mode)
{
    switch (mode) {
    case RESTCONF_WD_REPORT_ALL:
        return LYD_PRINT_WD_ALL;
    case RESTCONF_WD_TRIM:
        return LYD_PRINT_WD_TRIM;
    case RESTCONF_WD_REPORT_ALL_TAGGED:
        return LYD_PRINT_WD_ALL_TAG;
    case RESTCONF_WD_EXPLICIT:
    case RESTCONF_WD_UNSET:
    default:
        return LYD_PRINT_WD_EXPLICIT;
    }
}

/* --------------------------------------------------------------------
 * Parametre de requete "fields" (RFC 8040 SS4.8.3)
 * -------------------------------------------------------------------- */

/* Noeud d'une selection "fields" deja analysee : correspond a un
 * "api-identifier" du chemin, avec ses enfants explicitement selectionnes
 * ('children' non-NULL, cas d'un sous-selecteur parenthese ou d'un
 * segment de chemin suivant un '/') ou NULL, ce qui signifie "tout le
 * sous-arbre de ce noeud est selectionne" (cf. exemple RFC 8040 SS4.8.3
 * "fields=genre;year" : chaque terme sans parenthese est retenu en
 * entier). 'next' chaine les termes separes par ';' au meme niveau. */
struct field_node {
    char *module; /* NULL si le terme n'est pas qualifie par un module */
    char *name;
    struct field_node *children;
    struct field_node *next;
};

static void field_node_list_free(struct field_node *n)
{
    while (n) {
        struct field_node *next = n->next;
        field_node_list_free(n->children);
        free(n->module);
        free(n->name);
        free(n);
        n = next;
    }
}

struct field_parser {
    const char *s;
    size_t len;
    size_t pos;
};

static int fp_peek(const struct field_parser *p)
{
    return p->pos < p->len ? (unsigned char)p->s[p->pos] : -1;
}

/* Grammaire "identifier" de la RFC 8040 SS3.5.3.1 (reutilisee telle
 * quelle par "api-identifier" dans la grammaire "fields-expr", SS4.8.3):
 * (ALPHA / "_") *(ALPHA / DIGIT / "_" / "-" / ".") -- simplifie ici en
 * acceptant alnum/_/-/. partout (pas de verification stricte du premier
 * caractere), ce qui est legerement plus permissif que la RFC mais
 * suffisant en pratique. */
static int fp_is_ident_char(int c)
{
    return c == '_' || c == '-' || c == '.' || isalnum(c);
}

/* Analyse un "api-identifier" ([module-name ":"] identifier). Renvoie 0
 * et remplit *module_out (eventuellement NULL) / *name_out (chaines
 * allouees) en cas de succes ; -1 sinon (identifiant vide). */
static int fp_parse_identifier(struct field_parser *p, char **module_out, char **name_out)
{
    size_t start = p->pos;
    while (p->pos < p->len && fp_is_ident_char(fp_peek(p))) {
        p->pos++;
    }
    if (p->pos == start) {
        return -1;
    }
    char *id1 = strndup(p->s + start, p->pos - start);
    if (!id1) {
        return -1;
    }

    if (fp_peek(p) == ':') {
        p->pos++;
        size_t start2 = p->pos;
        while (p->pos < p->len && fp_is_ident_char(fp_peek(p))) {
            p->pos++;
        }
        if (p->pos == start2) {
            free(id1);
            return -1;
        }
        char *id2 = strndup(p->s + start2, p->pos - start2);
        if (!id2) {
            free(id1);
            return -1;
        }
        *module_out = id1;
        *name_out = id2;
    } else {
        *module_out = NULL;
        *name_out = id1;
    }
    return 0;
}

static struct field_node *fp_parse_expr(struct field_parser *p);

/* Analyse "path" = api-identifier ["/" path] en une chaine de field_node
 * a un seul enfant chacun (le dernier ayant 'children' a NULL, sauf si un
 * sous-selecteur parenthese est attache ensuite par fp_parse_term()).
 * *tail_out recoit le dernier (plus profond) noeud de la chaine, pour
 * permettre a l'appelant d'y accrocher un sous-selecteur. */
static struct field_node *fp_parse_path(struct field_parser *p, struct field_node **tail_out)
{
    char *module = NULL, *name = NULL;
    if (fp_parse_identifier(p, &module, &name) != 0) {
        return NULL;
    }
    struct field_node *node = calloc(1, sizeof(*node));
    if (!node) {
        free(module);
        free(name);
        return NULL;
    }
    node->module = module;
    node->name = name;

    if (fp_peek(p) == '/') {
        p->pos++;
        struct field_node *child_tail = NULL;
        struct field_node *child = fp_parse_path(p, &child_tail);
        if (!child) {
            field_node_list_free(node);
            return NULL;
        }
        node->children = child;
        *tail_out = child_tail;
    } else {
        *tail_out = node;
    }
    return node;
}

/* Analyse un "terme" = path ["(" fields-expr ")"]. */
static struct field_node *fp_parse_term(struct field_parser *p)
{
    struct field_node *tail = NULL;
    struct field_node *head = fp_parse_path(p, &tail);
    if (!head) {
        return NULL;
    }
    if (fp_peek(p) == '(') {
        p->pos++;
        struct field_node *sub = fp_parse_expr(p);
        if (!sub) {
            field_node_list_free(head);
            return NULL;
        }
        if (fp_peek(p) != ')') {
            field_node_list_free(head);
            field_node_list_free(sub);
            return NULL;
        }
        p->pos++;
        tail->children = sub;
    }
    return head;
}

/* Analyse "fields-expr" = terme (";" terme)*. */
static struct field_node *fp_parse_expr(struct field_parser *p)
{
    struct field_node *head = fp_parse_term(p);
    if (!head) {
        return NULL;
    }
    struct field_node *last = head;
    while (fp_peek(p) == ';') {
        p->pos++;
        struct field_node *next_term = fp_parse_term(p);
        if (!next_term) {
            field_node_list_free(head);
            return NULL;
        }
        last->next = next_term;
        last = next_term;
    }
    return head;
}

/* Point d'entree : analyse la valeur brute (deja percent-decodee par
 * http.c) du parametre "fields". Renvoie NULL si l'expression est
 * malformee ou vide (l'appelant doit alors renvoyer "invalid-value"). */
static struct field_node *parse_fields_param(const char *value)
{
    if (!value || !*value) {
        return NULL;
    }
    struct field_parser p;
    p.s = value;
    p.len = strlen(value);
    p.pos = 0;
    struct field_node *result = fp_parse_expr(&p);
    if (!result) {
        return NULL;
    }
    if (p.pos != p.len) {
        /* caracteres en trop non consommes par la grammaire */
        field_node_list_free(result);
        return NULL;
    }
    return result;
}

/* Applique une selection "fields" deja analysee a une liste de freres
 * lyd_node (*tree_head), en elaguant (lyd_free_tree()) tout noeud non
 * mentionne dans 'selection'. Un noeud mentionne SANS enfants explicites
 * dans 'selection' est conserve integralement (tout son sous-arbre,
 * conformement a l'exemple RFC 8040 SS4.8.3 "fields=genre;year") ; un
 * noeud mentionne AVEC des enfants explicites (sous-selecteur parenthese
 * ou chemin "/") voit son propre sous-arbre recursivement filtre de la
 * meme maniere. *tree_head est mis a jour si le tout premier frere de la
 * liste est elague.
 *
 * Un terme non qualifie par un module correspond a n'importe quel noeud
 * du meme nom local, quel que soit son module reel -- legerement plus
 * permissif que la RFC (qui suppose le module herite du contexte
 * ascendant), mais sans consequence pratique tant que les noms ne sont
 * pas ambigus entre modules a un meme niveau.
 *
 * XXX-LY-API : suppose que lyd_free_tree() unlink correctement le noeud
 * de la liste de ses freres (et met a jour le pointeur 'child' du parent
 * si necessaire), comme documente pour libyang 2.x ; verifiez ce
 * comportement dans votre libyang/tree_data.h si l'elagage produit un
 * arbre incoherent. */
static void apply_fields_filter(struct lyd_node **tree_head, const struct field_node *selection)
{
    struct lyd_node *n = tree_head ? *tree_head : NULL;
    while (n) {
        struct lyd_node *next = n->next;
        const struct field_node *match = NULL;
        if (n->schema) {
            for (const struct field_node *f = selection; f; f = f->next) {
                if (strcmp(f->name, n->schema->name) != 0) {
                    continue;
                }
                if (f->module && (!n->schema->module ||
                                   strcmp(f->module, n->schema->module->name) != 0)) {
                    continue;
                }
                match = f;
                break;
            }
        }
        if (!match) {
            if (n == *tree_head) {
                *tree_head = next;
            }
            lyd_free_tree(n);
        } else if (match->children) {
            struct lyd_node *child = lyd_child(n);
            apply_fields_filter(&child, match->children);
        }
        n = next;
    }
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
    sr_session_stop(session);

    if (frc != 0) {
        sr_release_context(g_conn);
        return -1;
    }

    /* "fields" (RFC 8040 SS4.8.3) : applique AVANT serialisation, tant que
     * le contexte libyang (schema) est encore acquis -- necessaire pour
     * comparer les noms de module des noeuds retournes a ceux de la
     * selection demandee. sr_data_t->tree est mis a jour en meme temps
     * que fr.tree pour que fetch_result_release()/sr_release_data() reste
     * coherent si le tout premier noeud de haut niveau a ete elague. */
    if (options && options->fields && fr.tree) {
        struct field_node *selection = parse_fields_param(options->fields);
        if (!selection) {
            fetch_result_release(&fr);
            sr_release_context(g_conn);
            err->error_type = strdup("protocol");
            err->error_tag = strdup("invalid-value");
            err->error_message = strdup("expression 'fields' malformee (RFC 8040 SS4.8.3)");
            return -1;
        }
        apply_fields_filter(&fr.tree, selection);
        if (fr.sr_data) {
            fr.sr_data->tree = fr.tree;
        }
        field_node_list_free(selection);
    }

    int wd_flag = with_defaults_print_flag(options ? options->with_defaults : RESTCONF_WD_UNSET);

    char *raw = NULL;
    if (fr.tree) {
        if (lyd_print_mem(&raw, fr.tree, LYD_JSON,
                          LYD_PRINT_SIBLINGS | LYD_PRINT_SHRINK | wd_flag) != 0) {
            fetch_result_release(&fr);
            sr_release_context(g_conn);
            err->error_type = strdup("application");
            err->error_tag = strdup("operation-failed");
            err->error_message = strdup("echec de serialisation JSON (lyd_print_mem)");
            return -1;
        }
    }
    fetch_result_release(&fr);
    sr_release_context(g_conn);

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

/* Cherche l'accolade fermante correspondant a s[start] (qui DOIT etre '{'),
 * en respectant les chaines JSON entre guillemets (pour ne pas compter des
 * accolades litterales a l'interieur d'une valeur de type string) et leurs
 * echappements ('\\'). Renvoie 0 et remplit *end_out (index de l'accolade
 * fermante) en cas de succes, -1 si aucune accolade fermante correspondante
 * n'est trouvee avant la fin de la chaine. */
static int find_matching_brace(const char *s, size_t len, size_t start, size_t *end_out)
{
    int depth = 0;
    int in_string = 0;
    for (size_t i = start; i < len; i++) {
        char c = s[i];
        if (in_string) {
            if (c == '\\') {
                i++; /* saute le caractere echappe, quel qu'il soit */
            } else if (c == '"') {
                in_string = 0;
            }
            continue;
        }
        if (c == '"') {
            in_string = 1;
        } else if (c == '{') {
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0) {
                *end_out = i;
                return 0;
            }
        }
    }
    return -1;
}

/* Deballe l'enveloppe JSON {"ietf-restconf:data": {...}} attendue par un
 * PUT/PATCH directement sur la racine d'une datastore (RFC 8040 SS4.5
 * Appendix B.2.3/B.2.4, ou l'exemple XML utilise <data
 * xmlns="urn:ietf:params:xml:ns:yang:ietf-restconf">...</data>). Ce
 * conteneur "data" n'est qu'un template YANG ("yang-data" extension, RFC
 * 8040 SS8, cf. module ietf-restconf) : il n'existe PAS comme noeud de
 * schema reel, donc lyd_parse_data() ne peut pas le reconnaitre directement
 * comme parent -- d'ou ce petit parseur JSON minimal (et non un parseur
 * JSON complet) qui se contente de retrouver la valeur associee a la cle
 * "ietf-restconf:data" au premier niveau, pour ensuite ne passer que cette
 * valeur (elle, un vrai objet de noeuds de donnees de haut niveau) au
 * parseur libyang normal, exactement comme pour un POST directement sur
 * {+restconf}/data (cf. sysrepo_backend_write(), cas 'attach_point NULL').
 *
 * *inner_start/*inner_len decrivent la sous-chaine de 'body' correspondant
 * a cette valeur (accolades incluses, ex. "{}" ou "{...}"), SANS copie ; a
 * dupliquer et NUL-terminer par l'appelant avant de la passer a
 * ly_in_new_memory(). Renvoie 0 en cas de succes ; -1 + *err (error-tag
 * "malformed-message") si l'enveloppe est absente ou mal formee. */
static int extract_data_envelope(const char *body, size_t len, const char **inner_start,
                                 size_t *inner_len, struct restconf_error *err)
{
    static const char *const key = "ietf-restconf:data";
    size_t klen = strlen(key);
    size_t i = 0;

    while (i < len && isspace((unsigned char)body[i])) {
        i++;
    }
    if (i >= len || body[i] != '{') {
        goto bad;
    }
    i++;
    while (i < len && isspace((unsigned char)body[i])) {
        i++;
    }
    if (i >= len || body[i] != '"') {
        goto bad;
    }
    i++;
    if (i + klen > len || strncmp(body + i, key, klen) != 0) {
        goto bad;
    }
    i += klen;
    if (i >= len || body[i] != '"') {
        goto bad;
    }
    i++;
    while (i < len && isspace((unsigned char)body[i])) {
        i++;
    }
    if (i >= len || body[i] != ':') {
        goto bad;
    }
    i++;
    while (i < len && isspace((unsigned char)body[i])) {
        i++;
    }
    if (i >= len || body[i] != '{') {
        /* On n'accepte que la forme objet ('{}' pour une datastore vide, ou
         * '{"module:noeud": ...}') ; une valeur scalaire/array au premier
         * niveau de "ietf-restconf:data" n'aurait de toute facon aucun sens
         * ici. */
        goto bad;
    }

    size_t obj_end = 0;
    if (find_matching_brace(body, len, i, &obj_end) != 0) {
        goto bad;
    }

    *inner_start = body + i;
    *inner_len = obj_end - i + 1;
    return 0;

bad:
    err->error_type = strdup("protocol");
    err->error_tag = strdup("malformed-message");
    err->error_message =
        strdup("corps de requete attendu sous la forme {\"ietf-restconf:data\": {...}} "
               "(RFC 8040 SS4.5 Appendix B.2.3/B.2.4)");
    return -1;
}

/* PUT (RESTCONF_WRITE_REPLACE) ou PATCH (RESTCONF_WRITE_MERGE) directement
 * sur la racine d'une datastore ({+restconf}/data ou {+restconf}/ds/<name>
 * eux-memes, nsegments == 0 cote appelant), RFC 8040 SS4.5 Appendix B.2.4
 * (PUT, remplacement complet) / SS4.6.1 + exemple B.2.3 (PATCH, fusion).
 *
 * Pour PUT : RFC 8040 SS4.5 exige que "tout noeud enfant absent de
 * l'element <data> mais present sur le serveur soit supprime". On ne
 * diffe explicitement qu'au niveau des noeuds de PREMIER niveau
 * (identifies via describe_child_segment(), qui inclut les valeurs de cle
 * pour les instances de liste) : pour un noeud de premier niveau present
 * dans les deux arbres, poser l'operation d'edition NETCONF "replace" sur
 * ce noeud (cf. plus bas) fait deja supprimer recursivement, cote sysrepo,
 * tout descendant absent du nouveau corps -- semantique standard
 * NETCONF <edit-config> avec nc:operation="replace". Seule l'ABSENCE
 * totale d'un noeud de premier niveau dans le nouveau corps necessite donc
 * une suppression explicite ici, via sr_delete_item() sur son xpath exact
 * (obtenu avec lyd_path(), XXX-LY-API : verifiez cette signature dans
 * votre libyang/tree_data.h).
 *
 * Pour PATCH ("plain patch"), aucune suppression n'est effectuee : les
 * noeuds de premier niveau du corps sont simplement fusionnes ('merge'),
 * conformement a la semantique "plain patch" (RFC 8040 SS4.6.1) qui ne
 * permet de creer/mettre a jour que ce qui est explicitement present dans
 * le corps.
 *
 * Non teste contre un sysrepo/libyang reels (cf. "Points sensibles a la
 * version installee" dans README.md) : la strategie de diff "premier
 * niveau + replace recursif" ci-dessus est une hypothese de conception a
 * valider, comme le reste de ce squelette. */
static int sysrepo_backend_write_datastore(int sr_ds, enum restconf_write_op op,
                                            const char *body_json, size_t body_len,
                                            struct restconf_error *err)
{
    if (!body_json || body_len == 0) {
        err->error_type = strdup("protocol");
        err->error_tag = strdup("malformed-message");
        err->error_message = strdup("corps de requete JSON attendu et absent");
        return -1;
    }

    const char *inner = NULL;
    size_t inner_len = 0;
    if (extract_data_envelope(body_json, body_len, &inner, &inner_len, err) != 0) {
        return -1;
    }

    char *inner_copy = malloc(inner_len + 1);
    if (!inner_copy) {
        err->error_type = strdup("application");
        err->error_tag = strdup("operation-failed");
        err->error_message = strdup("memoire insuffisante");
        return -1;
    }
    memcpy(inner_copy, inner, inner_len);
    inner_copy[inner_len] = '\0';

    sr_session_ctx_t *session = NULL;
    int rc = sr_session_start(g_conn, (sr_datastore_t)sr_ds, &session);
    if (rc != SR_ERR_OK) {
        free(inner_copy);
        err->error_type = strdup("application");
        err->error_tag = strdup("operation-failed");
        err->error_message = strdup(sr_strerror(rc));
        return -1;
    }

    const struct ly_ctx *ctx = sr_acquire_context(g_conn);
    if (!ctx) {
        free(inner_copy);
        sr_session_stop(session);
        err->error_type = strdup("application");
        err->error_tag = strdup("operation-failed");
        err->error_message = strdup("contexte libyang indisponible");
        return -1;
    }

    struct ly_in *in = NULL;
    if (ly_in_new_memory(inner_copy, &in) != LY_SUCCESS) {
        free(inner_copy);
        sr_release_context(g_conn);
        sr_session_stop(session);
        err->error_type = strdup("application");
        err->error_tag = strdup("operation-failed");
        err->error_message = strdup("echec d'allocation pour l'analyse du corps JSON");
        return -1;
    }

    struct lyd_node *new_tree = NULL;
    LY_ERR lyrc = lyd_parse_data(ctx, NULL, in, LYD_JSON, LYD_PARSE_ONLY | LYD_PARSE_NO_STATE, 0,
                                  &new_tree);
    ly_in_free(in, 0);
    free(inner_copy);

    if (lyrc != LY_SUCCESS) {
        if (new_tree) {
            lyd_free_all(new_tree);
        }
        sr_release_context(g_conn);
        sr_session_stop(session);
        err->error_type = strdup("protocol");
        err->error_tag = strdup("malformed-message");
        err->error_message = strdup("contenu de l'enveloppe 'ietf-restconf:data' non conforme "
                                     "au schema YANG charge (RFC 7951)");
        return -1;
    }

    if (op == RESTCONF_WRITE_REPLACE) {
        sr_data_t *old_data = NULL;
        rc = sr_get_data(session, "/*", 0, 0, 0, &old_data);
        if (rc != SR_ERR_OK && rc != SR_ERR_NOT_FOUND) {
            if (new_tree) {
                lyd_free_all(new_tree);
            }
            sr_release_context(g_conn);
            sr_session_stop(session);
            err->error_type = strdup("application");
            err->error_tag = strdup("operation-failed");
            err->error_message = strdup(sr_strerror(rc));
            return -1;
        }
        if (old_data && old_data->tree) {
            for (struct lyd_node *old_n = old_data->tree; old_n; old_n = old_n->next) {
                char *old_sig = describe_child_segment(old_n);
                int still_present = 0;
                for (struct lyd_node *new_n = new_tree; new_n && !still_present;
                     new_n = new_n->next) {
                    char *new_sig = describe_child_segment(new_n);
                    if (old_sig && new_sig && strcmp(old_sig, new_sig) == 0) {
                        still_present = 1;
                    }
                    free(new_sig);
                }
                if (!still_present) {
                    /* XXX-LY-API : lyd_path() (LYD_PATH_STD) renvoie le
                     * chemin de DONNEES exact de l'instance (avec ses
                     * predicats de cle) ; verifiez cette signature dans
                     * votre libyang/tree_data.h. */
                    char old_xpath[1024];
                    if (lyd_path(old_n, LYD_PATH_STD, old_xpath, sizeof(old_xpath))) {
                        sr_delete_item(session, old_xpath, 0);
                    }
                }
                free(old_sig);
            }
        }
        if (old_data) {
            sr_release_data(old_data);
        }
    }

    if (new_tree) {
        const char *op_attr = (op == RESTCONF_WRITE_REPLACE) ? "replace" : "merge";
        for (struct lyd_node *n = new_tree; n; n = n->next) {
            if (lyd_new_attr(n, "ietf-netconf", "ietf-netconf:operation", op_attr, NULL) !=
                LY_SUCCESS) {
                lyd_free_all(new_tree);
                sr_release_context(g_conn);
                sr_session_stop(session);
                err->error_type = strdup("application");
                err->error_tag = strdup("operation-failed");
                err->error_message = strdup("echec de marquage de l'operation d'edition");
                return -1;
            }
        }
        rc = sr_edit_batch(session, new_tree, "merge");
    } else {
        /* Enveloppe '{"ietf-restconf:data": {}}' : pas de nouveau contenu a
         * apporter (pour PUT, les eventuelles suppressions ci-dessus
         * suffisent a vider la datastore ; pour PATCH, c'est un no-op). */
        rc = SR_ERR_OK;
    }
    if (rc == SR_ERR_OK) {
        rc = sr_apply_changes(session, 0);
    }

    if (new_tree) {
        lyd_free_all(new_tree);
    }
    sr_release_context(g_conn);
    sr_session_stop(session);

    if (rc != SR_ERR_OK) {
        err->error_type = strdup("application");
        err->error_tag = strdup(write_error_tag(rc));
        err->error_message = strdup(sr_strerror(rc));
        return -1;
    }

    bump_datastore_revision(sr_ds);
    return 0;
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
         * {+restconf}/ds/<name> eux-memes) : RFC 8040 SS4.5 Appendix
         * B.2.4 (PUT) / SS4.6.1 + exemple B.2.3 (PATCH). */
        return sysrepo_backend_write_datastore(sr_ds, op, body_json, body_len, err);
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

    bump_datastore_revision(sr_ds);
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

    /* RFC 8040 SS3.4.1.3 : un DELETE reussi change aussi la datastore
     * (oubli initial ici : seules les ecritures via sysrepo_backend_write()
     * avancaient l'ETag/Last-Modified jusqu'ici). */
    bump_datastore_revision(sr_ds);
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

/* --------------------------------------------------------------------
 * RPC (RFC 8040 SS3.6/SS4.4.2) -- invocation de {+restconf}/operations/<op>
 * -------------------------------------------------------------------- */

/* Resout le noeud de SCHEMA (sans predicats de cle) correspondant a une
 * suite de segments RESTCONF, en suivant la meme regle d'heritage de
 * module que build_xpath() (un segment sans module herite du module du
 * segment precedent). Contrairement a build_xpath(), ne construit aucun
 * chemin de DONNEES et ne renvoie que le dernier noeud de schema
 * resolu -- utile pour determiner la NATURE du noeud cible (RPC,
 * action, conteneur, liste, ...) avant de decider comment le traiter.
 * Renvoie NULL si un segment ne correspond a aucun noeud du schema
 * charge (ou si le premier segment n'indique pas de module). */
static const struct lysc_node *resolve_schema_node(const struct ly_ctx *ctx,
                                                    const struct restconf_path_segment *segments,
                                                    size_t nsegments)
{
    char *schema_path = NULL;
    size_t len = 0, cap = 0;
    const char *current_module = NULL;
    const struct lysc_node *snode = NULL;

    for (size_t i = 0; i < nsegments; i++) {
        const struct restconf_path_segment *seg = &segments[i];
        const char *module = seg->module ? seg->module : current_module;
        if (!module) {
            free(schema_path);
            return NULL;
        }
        current_module = module;

        char seg_str[512];
        snprintf(seg_str, sizeof(seg_str), "/%s:%s", module, seg->name);
        if (str_append(&schema_path, &len, &cap, seg_str) != 0) {
            free(schema_path);
            return NULL;
        }

        snode = lys_find_path(ctx, NULL, schema_path, 0);
        if (!snode) {
            free(schema_path);
            return NULL;
        }
    }

    free(schema_path);
    return snode;
}

/* Recherche le conteneur schema special "output" (nodetype LYS_OUTPUT)
 * dans l'arbre renvoye par sr_rpc_send_tree() pour un RPC/action (XXX-SR-API :
 * sr_rpc_send_tree() renvoie un sr_data_t*, comme sr_get_data()/
 * sr_get_subtree(), pas un struct lyd_node* brut -- verifiez cette
 * signature dans votre sysrepo.h si elle differe), le serialise en JSON
 * (RFC 8040 SS3.6.2, "module:output") et LIBERE 'output' dans tous les
 * cas (succes comme echec) via sr_release_data(). *json_out recoit NULL
 * si la section 'output' est absente ou vide (l'appelant doit alors
 * repondre 204 No Content), ou le corps JSON alloue sinon. Renvoie 0 en
 * cas de succes, -1 + *err si la serialisation echoue. */
static int extract_operation_output(sr_data_t *output, char **json_out,
                                     struct restconf_error *err)
{
    *json_out = NULL;

    struct lyd_node *tree = output ? output->tree : NULL;
    if (!tree) {
        if (output) {
            sr_release_data(output);
        }
        return 0;
    }

    /* XXX-SR-API/XXX-LY-API : le conteneur 'output' peut se trouver a la
     * racine de l'arbre renvoye ou comme enfant du noeud RPC/action
     * selon la version de sysrepo/libyang -- on le recherche
     * explicitement plutot que de supposer sa position. */
    struct lyd_node *out_node = tree;
    if (!out_node->schema || out_node->schema->nodetype != LYS_OUTPUT) {
        struct lyd_node *child = lyd_child(out_node);
        out_node = NULL;
        for (; child; child = child->next) {
            if (child->schema && child->schema->nodetype == LYS_OUTPUT) {
                out_node = child;
                break;
            }
        }
    }

    if (!out_node || !lyd_child(out_node)) {
        sr_release_data(output);
        return 0;
    }

    char *raw = NULL;
    if (lyd_print_mem(&raw, out_node, LYD_JSON, LYD_PRINT_SHRINK) != 0) {
        sr_release_data(output);
        err->error_type = strdup("application");
        err->error_tag = strdup("operation-failed");
        err->error_message = strdup("echec de serialisation JSON de la sortie");
        return -1;
    }
    sr_release_data(output);

    /* RFC 8040 SS3.6.2 : la sortie est enveloppee dans un objet "output"
     * qualifie par le module -- ce qu'imprime deja lyd_print_mem() pour
     * ce noeud cible unique ("module:output": {...}). */
    *json_out = raw;
    return 0;
}

int sysrepo_backend_rpc_invoke(const struct restconf_path_segment *segments, size_t nsegments,
                               const char *body_json, size_t body_len, char **json_out,
                               struct restconf_error *err)
{
    *json_out = NULL;
    memset(err, 0, sizeof(*err));

    /* RFC 8040 SS3.6 : le chemin d'une operation RPC est
     * {+restconf}/operations/<module>:<rpc-name> -- un unique segment
     * qualifie par un module, sans valeurs de cle (les RPC ne sont pas
     * des listes). Les actions (statement YANG "action", invoquees via
     * {+restconf}/data/<...>/<action>) ne sont pas gerees ici : le
     * routage actuel de restconf_handler.c n'aiguille vers cette
     * fonction que depuis RESTCONF_RES_OPERATIONS. */
    if (nsegments != 1 || segments[0].nkeys != 0) {
        err->error_type = strdup("protocol");
        err->error_tag = strdup("invalid-value");
        err->error_message = strdup("chemin d'operation RESTCONF invalide (attendu : "
                                     "{+restconf}/operations/<module>:<rpc>)");
        return -1;
    }
    if (!segments[0].module) {
        err->error_type = strdup("protocol");
        err->error_tag = strdup("invalid-value");
        err->error_message = strdup("le nom de l'operation doit etre qualifie par un module "
                                     "(module:rpc-name)");
        return -1;
    }

    char rpc_path[512];
    snprintf(rpc_path, sizeof(rpc_path), "/%s:%s", segments[0].module, segments[0].name);

    /* sr_rpc_send_tree() n'est pas lie a une datastore particuliere ;
     * n'importe quelle sr_datastore_t convient pour ouvrir la session
     * (on choisit <operational>, coherent avec le fait qu'une invocation
     * RPC n'edite pas directement une configuration). */
    sr_session_ctx_t *session = NULL;
    int rc = sr_session_start(g_conn, SR_DS_OPERATIONAL, &session);
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

    /* Verifie que le chemin designe bien une operation RPC de haut
     * niveau (et pas, par exemple, un conteneur/liste ordinaire ou une
     * action) avant d'aller plus loin. */
    const struct lysc_node *rpc_snode = lys_find_path(ctx, NULL, rpc_path, 0);
    if (!rpc_snode || rpc_snode->nodetype != LYS_RPC) {
        sr_release_context(g_conn);
        sr_session_stop(session);
        err->error_type = strdup("protocol");
        err->error_tag = strdup("invalid-value");
        err->error_message = strdup("aucune operation RPC ne correspond a ce chemin");
        return -1;
    }

    /* Construit le noeud racine "rpc" lui-meme (sans donnees d'entree) :
     * c'est ce noeud, avec le corps JSON de la requete analyse comme
     * enfant direct (l'enveloppe "module:input", RFC 8040 SS3.6.1), qui
     * doit etre passe tel quel a sr_rpc_send_tree(). */
    struct lyd_node *rpc_node = NULL;
    LY_ERR lyrc = lyd_new_path(NULL, ctx, rpc_path, NULL, 0, &rpc_node);
    if (lyrc != LY_SUCCESS || !rpc_node) {
        sr_release_context(g_conn);
        sr_session_stop(session);
        err->error_type = strdup("application");
        err->error_tag = strdup("operation-failed");
        err->error_message = strdup("impossible de construire le noeud d'operation RPC");
        return -1;
    }

    if (body_json && body_len > 0) {
        struct ly_in *in = NULL;
        if (ly_in_new_memory(body_json, &in) != LY_SUCCESS) {
            lyd_free_tree(rpc_node);
            sr_release_context(g_conn);
            sr_session_stop(session);
            err->error_type = strdup("application");
            err->error_tag = strdup("operation-failed");
            err->error_message = strdup("echec d'allocation pour l'analyse du corps JSON");
            return -1;
        }

        struct lyd_node *parsed = NULL;
        lyrc = lyd_parse_data(ctx, rpc_node, in, LYD_JSON, LYD_PARSE_ONLY | LYD_PARSE_NO_STATE, 0,
                               &parsed);
        ly_in_free(in, 0);

        if (lyrc != LY_SUCCESS) {
            lyd_free_tree(rpc_node);
            sr_release_context(g_conn);
            sr_session_stop(session);
            err->error_type = strdup("protocol");
            err->error_tag = strdup("malformed-message");
            err->error_message = strdup("corps de requete JSON non conforme au schema 'input' "
                                         "de cette operation RPC (RFC 7951)");
            return -1;
        }
    }

    /* XXX-SR-API : sr_rpc_send_tree() prend ici directement le noeud RPC
     * (avec son eventuel enfant 'input' deja attache) et renvoie, via
     * 'output' (un sr_data_t*, comme sr_get_data()/sr_get_subtree() --
     * PAS un struct lyd_node* brut), l'arbre de sortie correspondant.
     * Verifiez cette signature exacte (ordre des parametres, unite du
     * timeout, type du dernier parametre) dans votre sysrepo.h installe :
     * elle a legerement varie entre versions de sysrepo 2.x. */
    sr_data_t *output = NULL;
    rc = sr_rpc_send_tree(session, rpc_node, 0, &output);

    lyd_free_tree(rpc_node);
    sr_release_context(g_conn);
    sr_session_stop(session);

    if (rc != SR_ERR_OK) {
        err->error_type = strdup("application");
        err->error_tag = strdup(rc == SR_ERR_UNAUTHORIZED
                                     ? "access-denied"
                                     : (rc == SR_ERR_INVAL_ARG ? "invalid-value"
                                                                : "operation-failed"));
        err->error_message = strdup(sr_strerror(rc));
        return -1;
    }

    return extract_operation_output(output, json_out, err);
}

int sysrepo_backend_is_action_path(const struct restconf_path_segment *segments, size_t nsegments)
{
    if (nsegments < 2) {
        return 0;
    }

    const struct ly_ctx *ctx = sr_acquire_context(g_conn);
    if (!ctx) {
        return 0;
    }

    const struct lysc_node *snode = resolve_schema_node(ctx, segments, nsegments);
    int is_action = snode && snode->nodetype == LYS_ACTION;

    sr_release_context(g_conn);
    return is_action;
}

int sysrepo_backend_action_invoke(const struct restconf_path_segment *segments, size_t nsegments,
                                  const char *body_json, size_t body_len, char **json_out,
                                  struct restconf_error *err)
{
    *json_out = NULL;
    memset(err, 0, sizeof(*err));

    /* RFC 8040 SS3.6 : le chemin d'une action est
     * {+restconf}/data/<...ancetres avec cles...>/<action-name> (ou,
     * pour ce serveur, l'equivalent sous {+restconf}/ds/<n>/...) --
     * 'segments' couvre donc TOUS ces segments, ancetres inclus, a la
     * difference d'un RPC de haut niveau qui n'en a qu'un seul. */
    if (nsegments < 2) {
        err->error_type = strdup("protocol");
        err->error_tag = strdup("invalid-value");
        err->error_message = strdup("chemin d'action invalide : une action doit etre invoquee "
                                     "sous une ressource de donnees parente");
        return -1;
    }

    sr_session_ctx_t *session = NULL;
    int rc = sr_session_start(g_conn, SR_DS_OPERATIONAL, &session);
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

    const struct lysc_node *action_snode = resolve_schema_node(ctx, segments, nsegments);
    if (!action_snode || action_snode->nodetype != LYS_ACTION) {
        sr_release_context(g_conn);
        sr_session_stop(session);
        err->error_type = strdup("protocol");
        err->error_tag = strdup("invalid-value");
        err->error_message = strdup("aucune action ne correspond a ce chemin");
        return -1;
    }

    /* build_xpath() resout aussi les valeurs de cle des ancetres (listes
     * ordered-by etc.) et n'ajoute pas de predicat pour le dernier
     * segment (l'action elle-meme, qui n'a jamais de cle -- seg->nkeys
     * == 0), ce qui produit exactement le chemin de DONNEES complet
     * jusqu'a l'instance de l'action visee. */
    char *full_xpath = NULL;
    if (build_xpath(ctx, segments, nsegments, &full_xpath, err) != 0) {
        sr_release_context(g_conn);
        sr_session_stop(session);
        return -1;
    }

    /* Construit le squelette d'ancetres (avec les cles des instances de
     * liste concernees) jusqu'au noeud d'action lui-meme -- meme demarche
     * et meme prudence que sysrepo_backend_write() (cf. XXX-LY-API :
     * CESNET/libyang#2337) : on re-resout le noeud cible via
     * lyd_find_path() plutot que de se fier au 'new_node' renvoye. */
    struct lyd_node *top = NULL, *new_node = NULL;
    LY_ERR lyrc = lyd_new_path2(NULL, ctx, full_xpath, NULL, 0, 0, 0, &top, &new_node);
    if (lyrc != LY_SUCCESS || !top) {
        free(full_xpath);
        sr_release_context(g_conn);
        sr_session_stop(session);
        err->error_type = strdup("protocol");
        err->error_tag = strdup("invalid-value");
        err->error_message = strdup("impossible de construire le chemin de l'action (incoherent "
                                     "avec le schema/les instances existantes)");
        return -1;
    }
    struct lyd_node *action_node = NULL;
    if (lyd_find_path(top, full_xpath, 0, &action_node) != LY_SUCCESS || !action_node) {
        action_node = new_node;
    }
    free(full_xpath);

    if (body_json && body_len > 0) {
        struct ly_in *in = NULL;
        if (ly_in_new_memory(body_json, &in) != LY_SUCCESS) {
            lyd_free_all(top);
            sr_release_context(g_conn);
            sr_session_stop(session);
            err->error_type = strdup("application");
            err->error_tag = strdup("operation-failed");
            err->error_message = strdup("echec d'allocation pour l'analyse du corps JSON");
            return -1;
        }

        struct lyd_node *parsed = NULL;
        lyrc = lyd_parse_data(ctx, action_node, in, LYD_JSON, LYD_PARSE_ONLY | LYD_PARSE_NO_STATE,
                               0, &parsed);
        ly_in_free(in, 0);

        if (lyrc != LY_SUCCESS) {
            lyd_free_all(top);
            sr_release_context(g_conn);
            sr_session_stop(session);
            err->error_type = strdup("protocol");
            err->error_tag = strdup("malformed-message");
            err->error_message = strdup("corps de requete JSON non conforme au schema 'input' "
                                         "de cette action (RFC 7951)");
            return -1;
        }
    }

    /* A la difference d'un RPC de haut niveau (ou l'arbre soumis est le
     * noeud rpc lui-meme), sr_rpc_send_tree() a besoin ici de l'arbre
     * COMPLET depuis la racine ('top') pour localiser l'instance de
     * donnees exacte (ex. quelle 'interface=eth0') sous laquelle
     * l'action est invoquee. 'output' est un sr_data_t* (cf. remarque
     * XXX-SR-API dans sysrepo_backend_rpc_invoke() ci-dessus). */
    sr_data_t *output = NULL;
    rc = sr_rpc_send_tree(session, top, 0, &output);

    lyd_free_all(top);
    sr_release_context(g_conn);
    sr_session_stop(session);

    if (rc != SR_ERR_OK) {
        err->error_type = strdup("application");
        err->error_tag = strdup(rc == SR_ERR_UNAUTHORIZED
                                     ? "access-denied"
                                     : (rc == SR_ERR_NOT_FOUND ? "invalid-value"
                                                                : "operation-failed"));
        err->error_message = strdup(sr_strerror(rc));
        return -1;
    }

    return extract_operation_output(output, json_out, err);
}
