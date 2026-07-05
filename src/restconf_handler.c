#include "restconf_handler.h"
#include "restconf_path.h"
#include "sysrepo_backend.h"
#include "errors.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <pthread.h>
#include <time.h>

const char *g_restconf_root = "/restconf";

static void send_error_status(struct http_response *resp, int status, const char *error_type,
                               const char *error_tag, const char *fmt, ...)
{
    char message[512];
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(message, sizeof(message), fmt, ap);
        va_end(ap);
    } else {
        message[0] = '\0';
    }

    char *body = restconf_error_single_json(error_type, error_tag, NULL,
                                             fmt ? "%s" : NULL, message);
    http_response_set_status(resp, status, "Error");
    http_response_set_body(resp, "application/yang-data+json", body,
                            body ? strlen(body) : 0);
}

static void send_restconf_error(struct http_response *resp, struct restconf_error *err,
                                 int status_override /* 0 = utiliser la table par defaut */)
{
    int status = status_override ? status_override
                                  : restconf_error_default_status(err->error_tag);
    char *body = restconf_errors_to_json(err, 1);
    http_response_set_status(resp, status, "Error");
    http_response_set_body(resp, "application/yang-data+json", body, body ? strlen(body) : 0);
    restconf_error_release(err);
}

static int is_get_or_head(const char *method)
{
    return method && (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0);
}

static int is_options(const char *method)
{
    return method && strcmp(method, "OPTIONS") == 0;
}

static void add_allow_header(struct http_response *resp, const char *allow)
{
    char header[256];
    snprintf(header, sizeof(header), "Allow: %s", allow);
    http_response_add_header(resp, header);
}

static void send_options(struct http_response *resp, const char *allow)
{
    http_response_set_status(resp, 204, "No Content");
    add_allow_header(resp, allow);
    http_response_set_body(resp, NULL, NULL, 0);
}

static void send_method_not_allowed(struct http_response *resp, const char *allow,
                                     const char *fmt, ...)
{
    char message[512];
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(message, sizeof(message), fmt, ap);
        va_end(ap);
    } else {
        message[0] = '\0';
    }

    char *body = restconf_error_single_json("protocol", "operation-not-supported", NULL,
                                             fmt ? "%s" : NULL, message);
    http_response_set_status(resp, 405, "Error");
    add_allow_header(resp, allow);
    http_response_set_body(resp, "application/yang-data+json", body, body ? strlen(body) : 0);
}

static int media_range_matches_json(const char *value, size_t len)
{
    while (len > 0 && isspace((unsigned char)*value)) {
        value++;
        len--;
    }
    while (len > 0 && isspace((unsigned char)value[len - 1])) {
        len--;
    }

    const char *semi = memchr(value, ';', len);
    if (semi) {
        len = (size_t)(semi - value);
        while (len > 0 && isspace((unsigned char)value[len - 1])) {
            len--;
        }
    }

    return (len == 3 && strncasecmp(value, "*/*", len) == 0) ||
           (len == 13 && strncasecmp(value, "application/*", len) == 0) ||
           (len == 26 && strncasecmp(value, "application/yang-data+json", len) == 0);
}

static int media_type_is_yang_json(const char *value, size_t len)
{
    while (len > 0 && isspace((unsigned char)*value)) {
        value++;
        len--;
    }
    while (len > 0 && isspace((unsigned char)value[len - 1])) {
        len--;
    }

    const char *semi = memchr(value, ';', len);
    if (semi) {
        len = (size_t)(semi - value);
        while (len > 0 && isspace((unsigned char)value[len - 1])) {
            len--;
        }
    }

    return len == 26 && strncasecmp(value, "application/yang-data+json", len) == 0;
}

static int accepts_yang_json(const struct http_request *req)
{
    if (!req->accept || !*req->accept) {
        return 1;
    }

    const char *p = req->accept;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (media_range_matches_json(p, len)) {
            return 1;
        }
        if (!comma) {
            break;
        }
        p = comma + 1;
    }

    return 0;
}

static int require_yang_json_accept(const struct http_request *req, struct http_response *resp)
{
    if (accepts_yang_json(req)) {
        return 0;
    }
    send_error_status(resp, 406, "protocol", "invalid-value",
                       "seul application/yang-data+json est actuellement disponible");
    return -1;
}

static int has_yang_json_content_type(const struct http_request *req)
{
    if (!req->content_type || !*req->content_type) {
        return 0;
    }
    return media_type_is_yang_json(req->content_type, strlen(req->content_type));
}

static int require_yang_json_content_type(const struct http_request *req,
                                           struct http_response *resp)
{
    if (has_yang_json_content_type(req)) {
        return 0;
    }
    send_error_status(resp, 415, "protocol", "invalid-value",
                       "seul Content-Type: application/yang-data+json est actuellement supporte");
    return -1;
}

static int query_param_allowed(const char *name, const char *const *allowed, size_t nallowed)
{
    if (!name) {
        return 0;
    }
    for (size_t i = 0; i < nallowed; i++) {
        if (strcmp(name, allowed[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int validate_query_params(const struct http_request *req, struct http_response *resp,
                                  const char *const *allowed, size_t nallowed)
{
    for (size_t i = 0; i < req->nparams; i++) {
        const char *name = req->params[i].name;
        if (!query_param_allowed(name, allowed, nallowed)) {
            send_error_status(resp, 400, "protocol", "invalid-value",
                               "parametre de requete non supporte : %s", name ? name : "");
            return -1;
        }
    }
    return 0;
}

static int validate_no_query_params(const struct http_request *req, struct http_response *resp)
{
    return validate_query_params(req, resp, NULL, 0);
}

/* --------------------------------------------------------------------
 * ETag / Last-Modified (RFC 8040 SS3.4.1 Edit Collision Prevention)
 * -------------------------------------------------------------------- */

/* Ajoute les en-tetes 'ETag' et 'Last-Modified' refletant l'etat courant
 * de la datastore sr_ds (RFC 8040 SS3.4.1.1/3.4.1.2 ; ce squelette ne
 * suit ces valeurs qu'au niveau de la datastore entiere, ce que la RFC
 * autorise explicitement en repli pour une ressource de donnees, cf.
 * SS3.5.1/3.5.2). Pas d'effet si sysrepo_backend_get_datastore_revision()
 * ne renvoie rien pour ce sr_ds (defensif, cf. REVISION_TABLE_SIZE cote
 * sysrepo_backend.c). */
static void add_datastore_revision_headers(struct http_response *resp, int sr_ds)
{
    char *etag = NULL;
    char *last_modified = NULL;
    sysrepo_backend_get_datastore_revision(sr_ds, &etag, &last_modified);
    if (etag) {
        char header[64];
        snprintf(header, sizeof(header), "ETag: %s", etag);
        http_response_add_header(resp, header);
        free(etag);
    }
    if (last_modified) {
        char header[64];
        snprintf(header, sizeof(header), "Last-Modified: %s", last_modified);
        http_response_add_header(resp, header);
        free(last_modified);
    }
}

/* Verifie les preconditions HTTP 'If-Match'/'If-Unmodified-Since' (RFC
 * 7232 SS3.1/SS3.4) avant une operation d'ecriture (RFC 8040 SS3.4.1,
 * "Edit Collision Prevention"). Si le client n'a fourni aucun des deux
 * en-tetes, l'operation peut toujours proceder (0). En cas d'echec de
 * precondition, renvoie une reponse '412 Precondition Failed' avec les
 * validateurs courants (RFC 7232 SS4.2) et renvoie -1 ; l'appelant DOIT
 * alors s'arreter sans effectuer l'ecriture. */
static int check_write_preconditions(const struct http_request *req, struct http_response *resp,
                                      int sr_ds)
{
    if (!req->if_match && !req->if_unmodified_since) {
        return 0;
    }
    if (!sysrepo_backend_check_preconditions(sr_ds, req->if_match, req->if_unmodified_since)) {
        return 0;
    }
    http_response_set_status(resp, 412, "Precondition Failed");
    add_datastore_revision_headers(resp, sr_ds);
    http_response_set_body(resp, NULL, NULL, 0);
    return -1;
}

/* Percent-encode un segment de chemin RESTCONF pour l'en-tete 'Location'
 * (RFC 8040 SS4.4.1). Les caracteres qui font partie de la syntaxe
 * 'api-path' elle-meme (':' separateur module, '=' et ',' pour les
 * valeurs de cle, RFC 8040 SS3.5.3.1) sont laisses tels quels ; le reste
 * (espaces, etc., cf. exemple 'Foo%20Fighters' de la RFC) est encode. */
static char *percent_encode_path_segment(const char *s)
{
    size_t len = strlen(s);
    char *out = malloc(len * 3 + 1);
    if (!out) {
        return NULL;
    }
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        int safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == ':' || c == '=' || c == ',' || c == '-' || c == '.' || c == '_' || c == '~';
        if (safe) {
            out[o++] = (char)c;
        } else {
            o += (size_t)sprintf(out + o, "%%%02X", c);
        }
    }
    out[o] = '\0';
    return out;
}

/* Construit l'en-tete 'Location' d'une reponse 201 a un POST (RFC 8040
 * SS4.4.1) en concatenant la racine {+restconf}, le chemin de la
 * ressource PARENTE (celle visee par l'URI POST, deja dans req->raw_path)
 * et le segment du nouveau noeud enfant. Renvoie NULL si 'child_segment'
 * est NULL (ex. echec de sa determination cote sysrepo_backend). */
static char *build_location(const struct http_request *req, const char *child_segment)
{
    if (!child_segment) {
        return NULL;
    }
    char *encoded = percent_encode_path_segment(child_segment);
    if (!encoded) {
        return NULL;
    }

    const char *raw = req->raw_path ? req->raw_path : "";
    size_t root_len = g_restconf_root ? strlen(g_restconf_root) : 0;
    if (root_len > 0 && strncmp(raw, g_restconf_root, root_len) == 0) {
        raw += root_len;
    }
    while (*raw == '/') {
        raw++;
    }

    size_t need = strlen(g_restconf_root) + 1 + strlen(raw) + 1 + strlen(encoded) + 1;
    char *loc = malloc(need);
    if (loc) {
        if (*raw) {
            snprintf(loc, need, "%s/%s/%s", g_restconf_root, raw, encoded);
        } else {
            snprintf(loc, need, "%s/%s", g_restconf_root, encoded);
        }
    }
    free(encoded);
    return loc;
}

static int parse_get_options(const struct http_request *req, struct restconf_get_options *options,
                              int is_operational_ds, struct http_response *resp)
{
    options->content = RESTCONF_CONTENT_ALL;
    options->depth = 0;
    options->fields = NULL;
    options->with_defaults = RESTCONF_WD_UNSET;
    options->with_origin = 0;

    const char *v = http_request_get_param(req, "content");
    if (v) {
        if (strcmp(v, "config") == 0) {
            options->content = RESTCONF_CONTENT_CONFIG;
        } else if (strcmp(v, "nonconfig") == 0) {
            options->content = RESTCONF_CONTENT_NONCONFIG;
        } else if (strcmp(v, "all") == 0) {
            options->content = RESTCONF_CONTENT_ALL;
        } else {
            send_error_status(resp, 400, "protocol", "invalid-value",
                               "valeur invalide pour le parametre content");
            return -1;
        }
    }

    v = http_request_get_param(req, "depth");
    if (v) {
        if (strcmp(v, "unbounded") == 0) {
            options->depth = 0;
        } else {
            for (const char *p = v; *p; p++) {
                if (!isdigit((unsigned char)*p)) {
                    send_error_status(resp, 400, "protocol", "invalid-value",
                                       "valeur invalide pour le parametre depth");
                    return -1;
                }
            }
            char *end = NULL;
            errno = 0;
            unsigned long depth = strtoul(v, &end, 10);
            if (errno != 0 || !end || *end != '\0' || depth == 0 || depth > UINT_MAX) {
                send_error_status(resp, 400, "protocol", "invalid-value",
                                   "valeur invalide pour le parametre depth");
                return -1;
            }
            options->depth = (unsigned int)depth;
        }
    }

    /* "fields" (RFC 8040 SS4.8.3) : la valeur brute (deja percent-decodee
     * par http.c) est transmise telle quelle a sysrepo_backend_get(), qui
     * l'analyse (grammaire fields-expr complete, y compris sous-selecteurs
     * parentheses) et l'applique a l'arbre libyang recupere, avant
     * serialisation JSON. Pas de validation de grammaire ici : une
     * expression malformee sera rejetee cote sysrepo_backend.c avec
     * "invalid-value". */
    options->fields = http_request_get_param(req, "fields");

    /* "with-defaults" (RFC 8040 SS4.8.9, table SS4.8.9 ; RFC 8527 SS3.2.1
     * pour le cas particulier de la datastore operational). Seules les 4
     * valeurs normalisees sont acceptees ; toute autre valeur est rejetee
     * explicitement (le serveur n'annonce pas "also-supported", RFC 8040
     * SS4.8.9 dernier paragraphe). */
    v = http_request_get_param(req, "with-defaults");
    if (v) {
        if (strcmp(v, "report-all") == 0) {
            options->with_defaults = RESTCONF_WD_REPORT_ALL;
        } else if (strcmp(v, "trim") == 0) {
            options->with_defaults = RESTCONF_WD_TRIM;
        } else if (strcmp(v, "explicit") == 0) {
            options->with_defaults = RESTCONF_WD_EXPLICIT;
        } else if (strcmp(v, "report-all-tagged") == 0) {
            options->with_defaults = RESTCONF_WD_REPORT_ALL_TAGGED;
        } else {
            send_error_status(resp, 400, "protocol", "invalid-value",
                               "valeur invalide pour le parametre with-defaults");
            return -1;
        }
    }

    /* "with-origin" (RFC 8527 SS3.2.2) : booleen (present => demande),
     * valide uniquement sur {+restconf}/ds/ietf-datastores:operational (ou
     * toute datastore derivee de l'identite "operational" -- ce squelette
     * n'en expose pas d'autre). Ailleurs : 400 invalid-value, exactement
     * comme le prescrit la RFC. */
    v = http_request_get_param(req, "with-origin");
    if (v) {
        if (!is_operational_ds) {
            send_error_status(resp, 400, "protocol", "invalid-value",
                               "le parametre with-origin n'est valide que sur "
                               "{+restconf}/ds/ietf-datastores:operational (RFC 8527 SS3.2.2)");
            return -1;
        }
        options->with_origin = 1;
    }

    return 0;
}

/* Indique si 'path' cible le sous-arbre "config false" (RFC 8040 SS9)
 * ietf-restconf-monitoring:restconf-state (capabilities, streams, ou tout
 * futur enfant), quel que soit le nombre de sous-segments demandes. */
static int is_restconf_monitoring_state_path(const struct restconf_request_path *path)
{
    if (path->nsegments < 1) {
        return 0;
    }
    return path->segments[0].module &&
           strcmp(path->segments[0].module, "ietf-restconf-monitoring") == 0 &&
           strcmp(path->segments[0].name, "restconf-state") == 0 &&
           path->segments[0].nkeys == 0;
}

static void handle_root(const struct http_request *req, struct http_response *resp)
{
    const char *allow = "GET, HEAD, OPTIONS";
    if (is_options(req->method)) {
        send_options(resp, allow);
        return;
    }
    if (!is_get_or_head(req->method)) {
        send_method_not_allowed(resp, allow,
                                 "seules les methodes GET/HEAD sont supportees sur la ressource "
                                 "API");
        return;
    }
    if (validate_no_query_params(req, resp) != 0) {
        return;
    }
    if (require_yang_json_accept(req, resp) != 0) {
        return;
    }
    /* On reutilise l'accesseur dedie pour rester coherent avec la
     * revision de module reellement chargee par sysrepo. */
    char *revision = NULL;
    struct restconf_error err;
    memset(&err, 0, sizeof(err));
    if (sysrepo_backend_get_yang_library_revision(&revision, &err) != 0) {
        send_restconf_error(resp, &err, 500);
        return;
    }

    size_t need = strlen(revision) + 128;
    char *body = malloc(need);
    if (!body) {
        free(revision);
        send_error_status(resp, 500, "application", "operation-failed", "memoire insuffisante");
        return;
    }
    snprintf(body, need,
             "{\"ietf-restconf:restconf\":{\"data\":{},\"operations\":{},"
             "\"yang-library-version\":\"%s\"}}",
             revision);
    free(revision);

    http_response_set_status(resp, 200, "OK");
    http_response_set_body(resp, "application/yang-data+json", body, strlen(body));
}

static void handle_yang_library_version(const struct http_request *req, struct http_response *resp)
{
    const char *allow = "GET, HEAD, OPTIONS";
    if (is_options(req->method)) {
        send_options(resp, allow);
        return;
    }
    if (!is_get_or_head(req->method)) {
        send_method_not_allowed(resp, allow, "seules les methodes GET/HEAD sont supportees ici");
        return;
    }
    if (validate_no_query_params(req, resp) != 0) {
        return;
    }
    if (require_yang_json_accept(req, resp) != 0) {
        return;
    }
    char *json = NULL;
    struct restconf_error err;
    memset(&err, 0, sizeof(err));
    if (sysrepo_backend_yang_library_version(&json, &err) != 0) {
        send_restconf_error(resp, &err, 500);
        return;
    }
    http_response_set_status(resp, 200, "OK");
    http_response_set_body(resp, "application/yang-data+json", json, strlen(json));
}

static void handle_data_get(const struct http_request *req, struct http_response *resp, int sr_ds,
                             const struct restconf_request_path *path, int is_operational_ds)
{
    static const char *const allowed_params[] = { "content", "depth", "fields", "with-defaults",
                                                   "with-origin" };

    if (validate_query_params(req, resp, allowed_params,
                               sizeof(allowed_params) / sizeof(allowed_params[0])) != 0) {
        return;
    }
    if (require_yang_json_accept(req, resp) != 0) {
        return;
    }

    struct restconf_get_options options;
    if (parse_get_options(req, &options, is_operational_ds, resp) != 0) {
        return;
    }

    char *json = NULL;
    struct restconf_error err;
    memset(&err, 0, sizeof(err));
    int rc = sysrepo_backend_get(sr_ds, path->segments, path->nsegments, &options, &json, &err);
    if (rc != 0) {
        int status = 0;
        if (err.error_tag && strcmp(err.error_tag, "invalid-value") == 0) {
            status = 404;
        }
        send_restconf_error(resp, &err, status);
        return;
    }

    /* RFC 8040 SS3.4.1.1/3.4.1.2 : le serveur DOIT renvoyer Last-Modified/
     * ETag pour une requete de recuperation sur la ressource datastore ;
     * ce squelette ne les distingue pas par ressource de donnees (repli
     * autorise par SS3.5.1/3.5.2), donc les memes valeurs sont renvoyees
     * pour toute lecture sous {+restconf}/data ou {+restconf}/ds/<name>. */
    add_datastore_revision_headers(resp, sr_ds);

    http_response_set_status(resp, 200, "OK");
    http_response_set_body(resp, "application/yang-data+json", json, strlen(json));
}

/* {+restconf}/data (RFC 8040, pre-NMDA) correspond ici a la datastore
 * <running> (config uniquement, cf. "Hypotheses de conception" dans
 * README.md) ; restconf-state est un sous-arbre "config false" (RFC 8040
 * SS9) qui n'y existe donc pas directement. Il est desormais fourni par
 * un plugin sysrepo (plugins/restconf_monitoring/, sr_oper_get_subscribe(),
 * cf. "Plugin sysrepo" dans README.md) comme n'importe quelle autre
 * donnee operationnelle : on redirige simplement la lecture vers la
 * datastore <operational> et on delegue a handle_data_get()/
 * sysrepo_backend_get(), sans plus construire de JSON a la main ici
 * (l'ancien handle_restconf_monitoring_capabilities() est supprime). */
static void handle_restconf_monitoring_state(const struct http_request *req,
                                              struct http_response *resp,
                                              const struct restconf_request_path *path)
{
    const char *allow = "GET, HEAD, OPTIONS";
    if (is_options(req->method)) {
        send_options(resp, allow);
        return;
    }
    if (!is_get_or_head(req->method)) {
        send_method_not_allowed(resp, allow,
                                 "restconf-state est une ressource operationnelle en lecture "
                                 "seule, fournie par un plugin sysrepo");
        return;
    }

    int sr_ds = 0;
    if (sysrepo_backend_datastore_from_identityref("ietf-datastores:operational", &sr_ds, NULL) !=
        0) {
        send_error_status(resp, 500, "application", "operation-failed",
                           "datastore operationnelle introuvable");
        return;
    }
    handle_data_get(req, resp, sr_ds, path, 1);
}

static void handle_data_post(const struct http_request *req, struct http_response *resp, int sr_ds,
                              const struct restconf_request_path *path)
{
    /* RFC 8040 SS3.4.1 : verifie If-Match/If-Unmodified-Since (s'ils sont
     * fournis) contre l'etat courant de la datastore AVANT toute ecriture. */
    if (check_write_preconditions(req, resp, sr_ds) != 0) {
        return;
    }

    int created_child_unused = 0;
    char *child_segment = NULL;
    struct restconf_error err;
    memset(&err, 0, sizeof(err));

    /* RFC 8040 SS4.4.1 : le corps DOIT contenir exactement une instance
     * de la ressource enfant a creer ; 'path' designe ici la ressource
     * PARENTE (la cible de l'URI POST). */
    int rc = sysrepo_backend_write(sr_ds, path->segments, path->nsegments, RESTCONF_WRITE_CREATE,
                                    req->body, req->body_len, &created_child_unused,
                                    &child_segment, &err);
    if (rc != 0) {
        send_restconf_error(resp, &err, 0);
        return;
    }

    char *location = build_location(req, child_segment);
    free(child_segment);

    http_response_set_status(resp, 201, "Created");
    if (location) {
        char header[1024];
        snprintf(header, sizeof(header), "Location: %s", location);
        http_response_add_header(resp, header);
        free(location);
    }
    /* RFC 8040 SS4.4.1 exemple Appendix B.2.1 : Last-Modified/ETag sur la
     * reponse 201, refletant l'etat de la datastore APRES l'ecriture. */
    add_datastore_revision_headers(resp, sr_ds);
    http_response_set_body(resp, NULL, NULL, 0);
}

static void handle_data_put(const struct http_request *req, struct http_response *resp, int sr_ds,
                             const struct restconf_request_path *path)
{
    /* path->nsegments == 0 : remplacement complet de la datastore (RFC
     * 8040 SS4.5, exemple Appendix B.2.4). sysrepo_backend_write()
     * aiguille elle-meme ce cas vers sysrepo_backend_write_datastore(),
     * qui deballe l'enveloppe JSON "ietf-restconf:data" du corps de
     * requete. La datastore elle-meme (en tant que ressource) existe
     * toujours deja : ce PUT est donc toujours un remplacement, jamais
     * une creation -> 204 No Content (created reste a 0). */
    if (check_write_preconditions(req, resp, sr_ds) != 0) {
        return;
    }

    int created = 0;
    struct restconf_error err;
    memset(&err, 0, sizeof(err));
    int rc = sysrepo_backend_write(sr_ds, path->segments, path->nsegments, RESTCONF_WRITE_REPLACE,
                                    req->body, req->body_len, &created, NULL, &err);
    if (rc != 0) {
        send_restconf_error(resp, &err, 0);
        return;
    }

    /* RFC 8040 SS4.5 : 201 Created si la ressource n'existait pas, sinon
     * 204 No Content. */
    if (created) {
        http_response_set_status(resp, 201, "Created");
    } else {
        http_response_set_status(resp, 204, "No Content");
    }
    /* RFC 8040 SS4.5 exemple Appendix B.2.4 : Last-Modified/ETag sur la
     * reponse, refletant l'etat de la datastore APRES l'ecriture. */
    add_datastore_revision_headers(resp, sr_ds);
    http_response_set_body(resp, NULL, NULL, 0);
}

static void handle_data_patch(const struct http_request *req, struct http_response *resp, int sr_ds,
                               const struct restconf_request_path *path)
{
    /* path->nsegments == 0 : fusion complete de la datastore (RFC 8040
     * SS4.6.1, exemple Appendix B.2.3, "plain patch" avec l'enveloppe
     * JSON "ietf-restconf:data"). sysrepo_backend_write() aiguille ce cas
     * vers sysrepo_backend_write_datastore() ; aucune suppression n'est
     * effectuee dans ce cas, conformement a la semantique "merge". */
    if (check_write_preconditions(req, resp, sr_ds) != 0) {
        return;
    }

    struct restconf_error err;
    memset(&err, 0, sizeof(err));
    int rc = sysrepo_backend_write(sr_ds, path->segments, path->nsegments, RESTCONF_WRITE_MERGE,
                                    req->body, req->body_len, NULL, NULL, &err);
    if (rc != 0) {
        send_restconf_error(resp, &err, 0);
        return;
    }

    /* RFC 8040 SS4.6 : 204 No Content (pas de corps de reponse pour un
     * "plain patch" qui reussit). */
    http_response_set_status(resp, 204, "No Content");
    /* RFC 8040 SS4.6.1 exemple Appendix B.2.3 : Last-Modified/ETag sur la
     * reponse, refletant l'etat de la datastore APRES la fusion. */
    add_datastore_revision_headers(resp, sr_ds);
    http_response_set_body(resp, NULL, NULL, 0);
}

static void handle_data_delete(const struct http_request *req, struct http_response *resp,
                                int sr_ds, const struct restconf_request_path *path)
{
    if (check_write_preconditions(req, resp, sr_ds) != 0) {
        return;
    }

    struct restconf_error err;
    memset(&err, 0, sizeof(err));
    int rc = sysrepo_backend_delete(sr_ds, path->segments, path->nsegments, &err);
    if (rc != 0) {
        int status = 0;
        if (err.error_tag && strcmp(err.error_tag, "invalid-value") == 0) {
            status = 404;
        }
        send_restconf_error(resp, &err, status);
        return;
    }
    http_response_set_status(resp, 204, "No Content");
    http_response_set_body(resp, NULL, NULL, 0);
}

/* RFC 8040 SS3.6 (action invoquee sous {+restconf}/data/<...>/<action>) +
 * RFC 8527 SS3.1 ("YANG actions can only be invoked in
 * {+restconf}/ds/ietf-datastores:operational") : meme structure que
 * handle_operation_invoke(), mais pour sysrepo_backend_action_invoke(),
 * qui a besoin du chemin COMPLET (ancetres + cles inclus) jusqu'a
 * l'action, pas d'un unique segment qualifie par un module. */
static void handle_action_invoke(const struct http_request *req, struct http_response *resp,
                                  const struct restconf_request_path *path)
{
    if (req->body && req->body_len > 0 && require_yang_json_content_type(req, resp) != 0) {
        return;
    }

    char *json_out = NULL;
    struct restconf_error err;
    memset(&err, 0, sizeof(err));
    int rc = sysrepo_backend_action_invoke(path->segments, path->nsegments, req->body,
                                            req->body_len, &json_out, &err);
    if (rc != 0) {
        send_restconf_error(resp, &err, 0);
        return;
    }

    if (json_out) {
        http_response_set_status(resp, 200, "OK");
        http_response_set_body(resp, "application/yang-data+json", json_out, strlen(json_out));
    } else {
        http_response_set_status(resp, 204, "No Content");
        http_response_set_body(resp, NULL, NULL, 0);
    }
}

static void handle_data_like(const struct http_request *req, struct http_response *resp,
                              int sr_ds, const struct restconf_request_path *path,
                              int is_operational_ds)
{
    /* PUT/PATCH sont maintenant supportes sur la racine d'une datastore
     * (remplacement/fusion complets, RFC 8040 SS4.5 Appendix B.2.4 / SS4.6.1
     * Appendix B.2.3) ; DELETE reste indefini sur cette meme racine (RFC
     * 8040 SS4.7), d'ou son absence de l'en-tete Allow quand nsegments == 0. */
    const char *allow = path->nsegments == 0
                            ? "GET, HEAD, POST, PUT, PATCH, OPTIONS"
                            : "GET, HEAD, POST, PUT, PATCH, DELETE, OPTIONS";
    if (is_options(req->method)) {
        send_options(resp, allow);
    } else if (is_get_or_head(req->method)) {
        handle_data_get(req, resp, sr_ds, path, is_operational_ds);
    } else if (strcmp(req->method, "POST") == 0) {
        if (validate_no_query_params(req, resp) != 0) {
            return;
        }
        /* Distingue une invocation d'action (statement YANG "action",
         * RFC 8040 SS3.6) d'une creation de ressource de donnees
         * ordinaire (POST SS4.4.1) : le dernier segment du chemin est
         * verifie contre le schema compile. */
        if (path->nsegments >= 2 &&
            sysrepo_backend_is_action_path(path->segments, path->nsegments)) {
            if (!is_operational_ds) {
                send_error_status(resp, 400, "protocol", "invalid-value",
                                   "les actions YANG ne peuvent etre invoquees que sous "
                                   "{+restconf}/ds/ietf-datastores:operational (RFC 8527 SS3.1)");
                return;
            }
            handle_action_invoke(req, resp, path);
            return;
        }
        if (require_yang_json_content_type(req, resp) != 0) {
            return;
        }
        handle_data_post(req, resp, sr_ds, path);
    } else if (strcmp(req->method, "PUT") == 0) {
        if (validate_no_query_params(req, resp) != 0) {
            return;
        }
        if (require_yang_json_content_type(req, resp) != 0) {
            return;
        }
        handle_data_put(req, resp, sr_ds, path);
    } else if (strcmp(req->method, "PATCH") == 0) {
        if (validate_no_query_params(req, resp) != 0) {
            return;
        }
        if (require_yang_json_content_type(req, resp) != 0) {
            return;
        }
        handle_data_patch(req, resp, sr_ds, path);
    } else if (strcmp(req->method, "DELETE") == 0) {
        if (validate_no_query_params(req, resp) != 0) {
            return;
        }
        if (path->nsegments == 0) {
            send_method_not_allowed(resp, allow,
                                     "DELETE n'est pas defini sur la racine d'une datastore");
            return;
        }
        handle_data_delete(req, resp, sr_ds, path);
    } else {
        send_method_not_allowed(resp, allow,
                                 "methode %s non supportee sur une ressource de donnees",
                                 req->method);
    }
}

static void handle_datastore(const struct http_request *req, struct http_response *resp,
                              const struct restconf_request_path *path)
{
    int sr_ds = 0, read_only = 0;
    if (sysrepo_backend_datastore_from_identityref(path->datastore_identityref, &sr_ds,
                                                    &read_only) != 0) {
        /* RFC 8527 SS3.2.2 : datastore invalide -> 400 invalid-value. */
        send_error_status(resp, 400, "protocol", "invalid-value",
                           "datastore inconnue ou non geree par ce serveur : %s",
                           path->datastore_identityref ? path->datastore_identityref : "?");
        return;
    }
    if (read_only && !is_get_or_head(req->method)) {
        /* Exception RFC 8527 SS3.1 : les actions restent invocables
         * (POST) sous operational meme si ce datastore est par ailleurs
         * en lecture seule pour les ecritures de donnees ordinaires. */
        int is_action_post = strcmp(req->method, "POST") == 0 && path->nsegments >= 2 &&
                              sysrepo_backend_is_action_path(path->segments, path->nsegments);
        if (!is_action_post) {
            const char *allow = "GET, HEAD, OPTIONS";
            if (is_options(req->method)) {
                send_options(resp, allow);
                return;
            }
            /* RFC 8527 SS3.2, 2e tiret : datastore en lecture seule par
             * nature -> 405 operation-not-supported. */
            send_method_not_allowed(resp, allow,
                                     "la datastore '%s' est en lecture seule sur ce serveur",
                                     path->datastore_identityref);
            return;
        }
    }
    int is_operational_ds = path->datastore_identityref &&
                             strcmp(path->datastore_identityref, "ietf-datastores:operational") == 0;
    handle_data_like(req, resp, sr_ds, path, is_operational_ds);
}

static void handle_operation_invoke(const struct http_request *req, struct http_response *resp,
                                     const struct restconf_request_path *path)
{
    /* RFC 8040 SS3.6 : {+restconf}/operations/<op> ne designe qu'une
     * operation RPC de haut niveau (un unique segment qualifie par un
     * module) ; les actions (invoquees sous {+restconf}/data/<...>) ne
     * sont pas routees ici. On rejette explicitement toute autre forme
     * plutot que de laisser sysrepo_backend_rpc_invoke() le faire, pour
     * renvoyer un message d'erreur plus precis a ce niveau. */
    if (path->nsegments != 1 || path->segments[0].nkeys != 0) {
        send_error_status(resp, 400, "protocol", "invalid-value",
                           "chemin d'operation invalide : attendu "
                           "{+restconf}/operations/<module>:<rpc-name>");
        return;
    }

    /* RFC 8040 SS3.6.1 : un corps de requete n'est requis que si le RPC a
     * une section 'input' avec des noeuds mandatoires ; on ne peut pas le
     * savoir sans consulter le schema, donc on n'exige pas de corps ici.
     * En revanche, si un corps EST fourni, il doit etre au bon format. */
    if (req->body && req->body_len > 0 && require_yang_json_content_type(req, resp) != 0) {
        return;
    }

    char *json_out = NULL;
    struct restconf_error err;
    memset(&err, 0, sizeof(err));
    int rc = sysrepo_backend_rpc_invoke(path->segments, path->nsegments, req->body,
                                         req->body_len, &json_out, &err);
    if (rc != 0) {
        send_restconf_error(resp, &err, 0);
        return;
    }

    if (json_out) {
        /* RFC 8040 SS4.4.2 : "200 OK" avec un corps si l'operation a une
         * sortie ('output', RFC 8040 SS3.6.2). */
        http_response_set_status(resp, 200, "OK");
        http_response_set_body(resp, "application/yang-data+json", json_out, strlen(json_out));
    } else {
        /* RFC 8040 SS4.4.2 : "204 No Content" si l'operation n'a pas de
         * message-body de sortie. */
        http_response_set_status(resp, 204, "No Content");
        http_response_set_body(resp, NULL, NULL, 0);
    }
}

/* --------------------------------------------------------------------
 * Flux SSE (RFC 8040 SS3.8/SS6, RFC 8527 n'etend pas ce mecanisme) :
 * {+restconf}/streams/<nom-de-flux>. Cf. feuille de route README.md,
 * Phase 2.
 * -------------------------------------------------------------------- */

/* Etat partage entre le thread de la requete HTTP (bloque dans la boucle
 * de handle_streams() ci-dessous) et le thread INTERNE cree par sysrepo
 * pour l'abonnement (qui invoque sse_notif_cb() de maniere asynchrone,
 * cf. sysrepo_backend_stream_subscribe()). Simple file FIFO protegee par
 * mutex+condvar : le thread de requete la vide et ecrit chaque message
 * en SSE des qu'il est reveille (notification recue) ou au bout de
 * SSE_HEARTBEAT_SECONDS (heartbeat, sert aussi a detecter une
 * deconnexion client via l'echec d'ecriture correspondant). */
struct sse_ctx {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    char **pending;
    size_t npending;
    size_t cap;
};

#define SSE_HEARTBEAT_SECONDS 15

static void sse_notif_cb(const char *json_notification, void *user_data)
{
    struct sse_ctx *ctx = user_data;
    pthread_mutex_lock(&ctx->mutex);
    if (ctx->npending == ctx->cap) {
        size_t ncap = ctx->cap ? ctx->cap * 2 : 8;
        char **np = realloc(ctx->pending, ncap * sizeof(char *));
        if (np) {
            ctx->pending = np;
            ctx->cap = ncap;
        }
    }
    if (ctx->npending < ctx->cap) {
        char *copy = strdup(json_notification);
        if (copy) {
            ctx->pending[ctx->npending++] = copy;
        }
    }
    /* Sinon (echec d'allocation) : notification silencieusement perdue --
     * pas de meilleure option sans bloquer indefiniment le thread
     * sysrepo qui invoque ce callback. */
    pthread_cond_signal(&ctx->cond);
    pthread_mutex_unlock(&ctx->mutex);
}

static void handle_streams(FCGX_Request *fcgx_request, const struct http_request *req,
                            struct http_response *resp, const struct restconf_request_path *path)
{
    const char *allow = "GET, HEAD, OPTIONS";
    if (is_options(req->method)) {
        send_options(resp, allow);
        return;
    }
    if (!is_get_or_head(req->method)) {
        send_method_not_allowed(resp, allow,
                                 "seules les methodes GET/HEAD sont supportees sur une "
                                 "ressource de flux");
        return;
    }
    if (validate_no_query_params(req, resp) != 0) {
        return;
    }
    if (!path->stream_name || !*path->stream_name) {
        send_error_status(resp, 400, "protocol", "invalid-value",
                           "nom de flux manquant : attendu {+restconf}/streams/<nom>");
        return;
    }
    if (!http_request_wants_event_stream(req)) {
        /* RFC 8040 SS3.8 : une ressource de flux d'evenements n'a de sens
         * qu'en SSE ; ce squelette rejette explicitement toute autre
         * negociation plutot que de renvoyer un contenu vide ou de
         * silencieusement ignorer la demande. */
        send_error_status(resp, 406, "protocol", "invalid-value",
                           "cette ressource ne peut etre recuperee qu'avec "
                           "'Accept: text/event-stream' (RFC 8040 SS6.3)");
        return;
    }
    if (strcmp(req->method, "HEAD") == 0) {
        /* HEAD sur un flux de duree indefinie n'a pas vraiment de sens ;
         * on renvoie les en-tetes sans ouvrir d'abonnement sysrepo. */
        http_response_set_status(resp, 200, "OK");
        http_response_add_header(resp, "Content-Type: text/event-stream");
        http_response_set_body(resp, NULL, NULL, 0);
        return;
    }

    struct sse_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    pthread_mutex_init(&ctx.mutex, NULL);
    pthread_cond_init(&ctx.cond, NULL);

    struct sysrepo_stream_subscription *sub = NULL;
    struct restconf_error err;
    memset(&err, 0, sizeof(err));
    if (sysrepo_backend_stream_subscribe(path->stream_name, sse_notif_cb, &ctx, &sub, &err) != 0) {
        int status = 0;
        if (err.error_tag && strcmp(err.error_tag, "invalid-value") == 0) {
            status = 404;
        }
        send_restconf_error(resp, &err, status);
        pthread_cond_destroy(&ctx.cond);
        pthread_mutex_destroy(&ctx.mutex);
        return;
    }

    /* A partir d'ici, la reponse est ecrite directement sur 'fcgx_request'
     * (en-tetes SSE, puis chaque evenement au fil de l'eau) : main.c ne
     * doit plus toucher a '*resp' au-dela de 'already_sent'. */
    http_sse_send_headers(fcgx_request);
    resp->already_sent = 1;

    int alive = 1;
    while (alive) {
        pthread_mutex_lock(&ctx.mutex);
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += SSE_HEARTBEAT_SECONDS;
        while (ctx.npending == 0) {
            int rc = pthread_cond_timedwait(&ctx.cond, &ctx.mutex, &deadline);
            if (rc == ETIMEDOUT) {
                break;
            }
        }
        char **to_send = ctx.pending;
        size_t nto_send = ctx.npending;
        ctx.pending = NULL;
        ctx.npending = 0;
        ctx.cap = 0;
        pthread_mutex_unlock(&ctx.mutex);

        if (nto_send > 0) {
            for (size_t i = 0; i < nto_send; i++) {
                if (alive && http_sse_send_event(fcgx_request, to_send[i]) != 0) {
                    /* Client deconnecte (echec d'ecriture, ex. EPIPE) :
                     * on cesse d'ecrire mais on finit de liberer la file
                     * courante avant de sortir de la boucle externe. */
                    alive = 0;
                }
                free(to_send[i]);
            }
            free(to_send);
        } else {
            /* Timeout sans notification recue : heartbeat SSE (ligne de
             * commentaire ignoree par tout client conforme), sert aussi
             * a detecter une deconnexion pendant les periodes calmes. */
            if (http_sse_send_comment(fcgx_request, "keep-alive") != 0) {
                alive = 0;
            }
        }
    }

    /* sysrepo_backend_stream_unsubscribe() bloque jusqu'a ce qu'aucun
     * appel a sse_notif_cb() ne soit plus en cours ni a venir pour cet
     * abonnement : purger 'ctx.pending' seulement APRES cet appel evite
     * toute fuite memoire due a une derniere notification arrivee juste
     * avant le desabonnement. */
    sysrepo_backend_stream_unsubscribe(sub);
    for (size_t i = 0; i < ctx.npending; i++) {
        free(ctx.pending[i]);
    }
    free(ctx.pending);
    pthread_cond_destroy(&ctx.cond);
    pthread_mutex_destroy(&ctx.mutex);
}

static void handle_operations(const struct http_request *req, struct http_response *resp,
                               const struct restconf_request_path *path)
{
    const char *allow = "POST, OPTIONS";
    if (is_options(req->method)) {
        send_options(resp, allow);
        return;
    }
    if (is_get_or_head(req->method)) {
        /* RFC 8040 SS4.3 : GET sur une ressource d'operation -> 405. */
        send_method_not_allowed(resp, allow,
                                 "les ressources d'operation ne supportent pas GET/HEAD");
        return;
    }
    if (strcmp(req->method, "POST") == 0) {
        if (validate_no_query_params(req, resp) != 0) {
            return;
        }
        if (path->nsegments == 0) {
            /* POST sur {+restconf}/operations lui-meme (sans operation
             * ciblee) : RFC 8040 SS3.3.2 n'en fait qu'un conteneur de
             * decouverte (GET), pas une ressource invocable. */
            send_error_status(resp, 400, "protocol", "invalid-value",
                               "aucune operation ciblee : POST attendu sur "
                               "{+restconf}/operations/<module>:<rpc-name>");
            return;
        }
        handle_operation_invoke(req, resp, path);
        return;
    }

    send_method_not_allowed(resp, allow,
                             "methode %s non supportee sur une ressource d'operation",
                             req->method);
}

void restconf_handle_request(FCGX_Request *fcgx_request, const struct http_request *req,
                              struct http_response *resp)
{
    if (strcmp(req->path, "/.well-known/host-meta") == 0) {
        const char *allow = "GET, HEAD, OPTIONS";
        if (is_options(req->method)) {
            send_options(resp, allow);
            return;
        }
        if (!is_get_or_head(req->method)) {
            send_method_not_allowed(resp, allow, NULL);
            return;
        }
        if (validate_no_query_params(req, resp) != 0) {
            return;
        }
        char body[256];
        int n = snprintf(body, sizeof(body),
                          "<XRD xmlns='http://docs.oasis-open.org/ns/xri/xrd-1.0'>"
                          "<Link rel='restconf' href='%s'/></XRD>",
                          g_restconf_root);
        http_response_set_status(resp, 200, "OK");
        http_response_set_body(resp, "application/xrd+xml", strdup(body), (size_t)n);
        return;
    }

    struct restconf_request_path path;
    struct restconf_error perr;
    memset(&perr, 0, sizeof(perr));

    if (restconf_path_parse(g_restconf_root, req->raw_path, &path, &perr) != 0) {
        int status = (path.type == RESTCONF_RES_UNKNOWN) ? 404 : 400;
        send_restconf_error(resp, &perr, status);
        restconf_path_free(&path);
        return;
    }

    switch (path.type) {
    case RESTCONF_RES_ROOT:
        handle_root(req, resp);
        break;
    case RESTCONF_RES_YANG_LIBRARY_VERSION:
        handle_yang_library_version(req, resp);
        break;
    case RESTCONF_RES_STREAMS:
        handle_streams(fcgx_request, req, resp, &path);
        break;
    case RESTCONF_RES_DATA:
        if (is_restconf_monitoring_state_path(&path)) {
            handle_restconf_monitoring_state(req, resp, &path);
        } else {
            /* {+restconf}/data n'est pas la datastore operational (cf.
             * hypotheses de mapping dans README.md) : RFC 8527 SS3.1
             * n'autorise donc pas d'y invoquer une action. */
            handle_data_like(req, resp, sysrepo_backend_default_data_datastore(), &path, 0);
        }
        break;
    case RESTCONF_RES_DATASTORE:
        handle_datastore(req, resp, &path);
        break;
    case RESTCONF_RES_OPERATIONS:
        handle_operations(req, resp, &path);
        break;
    default:
        send_error_status(resp, 404, "protocol", "invalid-value", "ressource inconnue");
        break;
    }

    restconf_path_free(&path);
}
