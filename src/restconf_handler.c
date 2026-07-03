#include "restconf_handler.h"
#include "restconf_path.h"
#include "sysrepo_backend.h"
#include "errors.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

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

static enum restconf_content_mode parse_content_param(const struct http_request *req)
{
    const char *v = http_request_get_param(req, "content");
    if (!v) {
        return RESTCONF_CONTENT_ALL;
    }
    if (strcmp(v, "config") == 0) {
        return RESTCONF_CONTENT_CONFIG;
    }
    if (strcmp(v, "nonconfig") == 0) {
        return RESTCONF_CONTENT_NONCONFIG;
    }
    return RESTCONF_CONTENT_ALL;
}

static void handle_root(const struct http_request *req, struct http_response *resp)
{
    if (!is_get_or_head(req->method)) {
        send_error_status(resp, 405, "protocol", "operation-not-supported",
                           "seules les methodes GET/HEAD sont supportees sur la ressource API");
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

static void handle_data_like(const struct http_request *req, struct http_response *resp,
                              int sr_ds, const struct restconf_request_path *path)
{
    if (!is_get_or_head(req->method)) {
        /* Phase 1 : lecture seule. POST/PUT/PATCH/DELETE sur les
         * ressources de donnees seront ajoutes dans une phase
         * ulterieure (edit via sr_edit_batch()/sr_apply_changes()). */
        send_error_status(resp, 501, "application", "operation-not-supported",
                           "methode %s non encore implementee (squelette phase 1, lecture seule)",
                           req->method);
        return;
    }

    enum restconf_content_mode content = parse_content_param(req);

    char *json = NULL;
    struct restconf_error err;
    memset(&err, 0, sizeof(err));
    int rc = sysrepo_backend_get(sr_ds, path->segments, path->nsegments, content, &json, &err);
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
        handle_data_like(req, resp, sysrepo_backend_default_data_datastore(), &path);
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
