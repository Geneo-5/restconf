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
                              struct http_response *resp)
{
    options->content = RESTCONF_CONTENT_ALL;
    options->depth = 0;

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

    return 0;
}

static int is_restconf_monitoring_capabilities(const struct restconf_request_path *path)
{
    if (path->nsegments < 1 || path->nsegments > 3) {
        return 0;
    }
    if (!path->segments[0].module ||
        strcmp(path->segments[0].module, "ietf-restconf-monitoring") != 0 ||
        strcmp(path->segments[0].name, "restconf-state") != 0 ||
        path->segments[0].nkeys != 0) {
        return 0;
    }
    if (path->nsegments >= 2 &&
        (path->segments[1].module ||
         strcmp(path->segments[1].name, "capabilities") != 0 ||
         path->segments[1].nkeys != 0)) {
        return 0;
    }
    if (path->nsegments == 3 &&
        (path->segments[2].module ||
         strcmp(path->segments[2].name, "capability") != 0 ||
         path->segments[2].nkeys != 0)) {
        return 0;
    }
    return 1;
}

static void handle_restconf_monitoring_capabilities(const struct http_request *req,
                                                     struct http_response *resp,
                                                     const struct restconf_request_path *path)
{
    if (!is_get_or_head(req->method)) {
        send_error_status(resp, 405, "protocol", "operation-not-supported",
                           "restconf-state/capabilities est une ressource operationnelle en "
                           "lecture seule");
        return;
    }
    if (require_yang_json_accept(req, resp) != 0) {
        return;
    }

    const char *capabilities =
        "\"urn:ietf:params:restconf:capability:defaults:1.0?basic-mode=explicit\","
        "\"urn:ietf:params:restconf:capability:depth:1.0\"";
    const char *fmt = NULL;
    if (path->nsegments == 1) {
        fmt = "{\"ietf-restconf-monitoring:restconf-state\":{\"capabilities\":"
              "{\"capability\":[%s]}}}";
    } else if (path->nsegments == 2) {
        fmt = "{\"ietf-restconf-monitoring:capabilities\":{\"capability\":[%s]}}";
    } else {
        fmt = "{\"ietf-restconf-monitoring:capability\":[%s]}";
    }

    size_t need = strlen(fmt) + strlen(capabilities) + 1;
    char *body = malloc(need);
    if (!body) {
        send_error_status(resp, 500, "application", "operation-failed", "memoire insuffisante");
        return;
    }
    snprintf(body, need, fmt, capabilities);

    http_response_set_status(resp, 200, "OK");
    http_response_set_body(resp, "application/yang-data+json", body, strlen(body));
}

static void handle_root(const struct http_request *req, struct http_response *resp)
{
    if (!is_get_or_head(req->method)) {
        send_error_status(resp, 405, "protocol", "operation-not-supported",
                           "seules les methodes GET/HEAD sont supportees sur la ressource API");
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
    if (!is_get_or_head(req->method)) {
        send_error_status(resp, 405, "protocol", "operation-not-supported",
                           "seules les methodes GET/HEAD sont supportees ici");
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
                             const struct restconf_request_path *path)
{
    if (require_yang_json_accept(req, resp) != 0) {
        return;
    }

    struct restconf_get_options options;
    if (parse_get_options(req, &options, resp) != 0) {
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

    http_response_set_status(resp, 200, "OK");
    http_response_set_body(resp, "application/yang-data+json", json, strlen(json));
}

static void handle_data_post(const struct http_request *req, struct http_response *resp, int sr_ds,
                              const struct restconf_request_path *path)
{
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
    http_response_set_body(resp, NULL, NULL, 0);
}

static void handle_data_put(const struct http_request *req, struct http_response *resp, int sr_ds,
                             const struct restconf_request_path *path)
{
    if (path->nsegments == 0) {
        /* Remplacement de la datastore entiere (RFC 8040 SS4.5, exemple
         * Appendix B.2.4) : non gere par ce squelette, cf. sysrepo_backend.c. */
        send_error_status(resp, 501, "application", "operation-not-supported",
                           "remplacement complet de la datastore via PUT non encore implemente");
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
    http_response_set_body(resp, NULL, NULL, 0);
}

static void handle_data_patch(const struct http_request *req, struct http_response *resp, int sr_ds,
                               const struct restconf_request_path *path)
{
    if (path->nsegments == 0) {
        send_error_status(resp, 400, "protocol", "invalid-value",
                           "PATCH necessite une ressource de donnees cible, pas la racine de "
                           "la datastore");
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
    http_response_set_body(resp, NULL, NULL, 0);
}

static void handle_data_delete(struct http_response *resp, int sr_ds,
                                const struct restconf_request_path *path)
{
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

static void handle_data_like(const struct http_request *req, struct http_response *resp,
                              int sr_ds, const struct restconf_request_path *path)
{
    if (is_get_or_head(req->method)) {
        handle_data_get(req, resp, sr_ds, path);
    } else if (strcmp(req->method, "POST") == 0) {
        if (require_yang_json_content_type(req, resp) != 0) {
            return;
        }
        handle_data_post(req, resp, sr_ds, path);
    } else if (strcmp(req->method, "PUT") == 0) {
        if (require_yang_json_content_type(req, resp) != 0) {
            return;
        }
        handle_data_put(req, resp, sr_ds, path);
    } else if (strcmp(req->method, "PATCH") == 0) {
        if (require_yang_json_content_type(req, resp) != 0) {
            return;
        }
        handle_data_patch(req, resp, sr_ds, path);
    } else if (strcmp(req->method, "DELETE") == 0) {
        handle_data_delete(resp, sr_ds, path);
    } else {
        send_error_status(resp, 405, "protocol", "operation-not-supported",
                           "methode %s non supportee sur une ressource de donnees", req->method);
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
        /* RFC 8527 SS3.2, 2e tiret : datastore en lecture seule par
         * nature -> 405 operation-not-supported. */
        send_error_status(resp, 405, "protocol", "operation-not-supported",
                           "la datastore '%s' est en lecture seule sur ce serveur",
                           path->datastore_identityref);
        return;
    }
    handle_data_like(req, resp, sr_ds, path);
}

static void handle_operations(const struct http_request *req, struct http_response *resp,
                               const struct restconf_request_path *path)
{
    (void)path;
    if (is_get_or_head(req->method)) {
        /* RFC 8040 SS4.3 : GET sur une ressource d'operation -> 405. */
        send_error_status(resp, 405, "protocol", "operation-not-supported",
                           "les ressources d'operation ne supportent pas GET/HEAD");
        return;
    }
    /* L'invocation de RPC/action (POST) sera ajoutee dans une phase
     * ulterieure via sr_rpc_send_tree(). */
    send_error_status(resp, 501, "application", "operation-not-supported",
                       "invocation de RPC/action non encore implementee (squelette phase 1)");
}

void restconf_handle_request(const struct http_request *req, struct http_response *resp)
{
    if (strcmp(req->path, "/.well-known/host-meta") == 0) {
        if (!is_get_or_head(req->method)) {
            send_error_status(resp, 405, "protocol", "operation-not-supported", NULL);
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
    case RESTCONF_RES_DATA:
        if (is_restconf_monitoring_capabilities(&path)) {
            handle_restconf_monitoring_capabilities(req, resp, &path);
        } else {
            handle_data_like(req, resp, sysrepo_backend_default_data_datastore(), &path);
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
