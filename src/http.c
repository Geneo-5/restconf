#include "http.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *xstrdup(const char *s)
{
    if (!s) {
        return NULL;
    }
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) {
        memcpy(p, s, n);
    }
    return p;
}

char *http_percent_decode(const char *s, size_t len)
{
    char *out = malloc(len + 1);
    if (!out) {
        return NULL;
    }
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '%' && i + 2 < len && isxdigit((unsigned char)s[i + 1]) &&
            isxdigit((unsigned char)s[i + 2])) {
            char hex[3] = { s[i + 1], s[i + 2], 0 };
            out[o++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            out[o++] = s[i];
        }
    }
    out[o] = '\0';
    return out;
}

static void parse_query_string(struct http_request *req)
{
    req->params = NULL;
    req->nparams = 0;
    if (!req->query_string || !*req->query_string) {
        return;
    }

    /* Compte le nombre de paires pour dimensionner le tableau. */
    size_t count = 1;
    for (const char *p = req->query_string; *p; p++) {
        if (*p == '&') {
            count++;
        }
    }

    struct http_query_param *params = calloc(count, sizeof(*params));
    if (!params) {
        return;
    }

    size_t idx = 0;
    const char *start = req->query_string;
    while (start) {
        const char *amp = strchr(start, '&');
        size_t pair_len = amp ? (size_t)(amp - start) : strlen(start);
        const char *eq = memchr(start, '=', pair_len);

        size_t name_len = eq ? (size_t)(eq - start) : pair_len;
        params[idx].name = http_percent_decode(start, name_len);

        if (eq) {
            size_t val_off = name_len + 1;
            size_t val_len = pair_len - val_off;
            params[idx].value = http_percent_decode(start + val_off, val_len);
        } else {
            params[idx].value = xstrdup("");
        }
        idx++;
        start = amp ? amp + 1 : NULL;
    }

    req->params = params;
    req->nparams = idx;
}

const char *http_request_get_param(const struct http_request *req, const char *name)
{
    for (size_t i = 0; i < req->nparams; i++) {
        if (req->params[i].name && strcmp(req->params[i].name, name) == 0) {
            return req->params[i].value;
        }
    }
    return NULL;
}

int http_request_from_fcgx(FCGX_Request *request, struct http_request *out)
{
    memset(out, 0, sizeof(*out));

    const char *method = FCGX_GetParam("REQUEST_METHOD", request->envp);
    out->method = xstrdup(method ? method : "GET");

    /* PATH_INFO est le chemin apres le point de montage du script (ex:
     * derriere nginx avec fastcgi_split_path_info). A defaut on retombe
     * sur REQUEST_URI (sans la query string). */
    const char *path_info = FCGX_GetParam("PATH_INFO", request->envp);
    if (!path_info || !*path_info) {
        const char *req_uri = FCGX_GetParam("REQUEST_URI", request->envp);
        if (req_uri) {
            const char *qm = strchr(req_uri, '?');
            size_t len = qm ? (size_t)(qm - req_uri) : strlen(req_uri);
            char *tmp = malloc(len + 1);
            if (tmp) {
                memcpy(tmp, req_uri, len);
                tmp[len] = '\0';
                out->raw_path = tmp;
            }
        }
    } else {
        out->raw_path = xstrdup(path_info);
    }
    if (!out->raw_path) {
        out->raw_path = xstrdup("/");
    }
    out->path = http_percent_decode(out->raw_path, strlen(out->raw_path));

    const char *qs = FCGX_GetParam("QUERY_STRING", request->envp);
    out->query_string = xstrdup(qs ? qs : "");
    parse_query_string(out);

    out->content_type = xstrdup(FCGX_GetParam("CONTENT_TYPE", request->envp));
    out->accept = xstrdup(FCGX_GetParam("HTTP_ACCEPT", request->envp));
    out->if_match = xstrdup(FCGX_GetParam("HTTP_IF_MATCH", request->envp));
    out->if_unmodified_since = xstrdup(FCGX_GetParam("HTTP_IF_UNMODIFIED_SINCE", request->envp));

    const char *cl = FCGX_GetParam("CONTENT_LENGTH", request->envp);
    long content_length = cl ? strtol(cl, NULL, 10) : 0;
    if (content_length > 0) {
        char *body = malloc((size_t)content_length + 1);
        if (body) {
            int total = 0;
            while (total < content_length) {
                int n = FCGX_GetStr(body + total, (int)content_length - total, request->in);
                if (n <= 0) {
                    break;
                }
                total += n;
            }
            body[total] = '\0';
            out->body = body;
            out->body_len = (size_t)total;
        }
    }

    return 0;
}

void http_request_free(struct http_request *req)
{
    if (!req) {
        return;
    }
    free(req->method);
    free(req->raw_path);
    free(req->path);
    free(req->query_string);
    for (size_t i = 0; i < req->nparams; i++) {
        free(req->params[i].name);
        free(req->params[i].value);
    }
    free(req->params);
    free(req->content_type);
    free(req->accept);
    free(req->if_match);
    free(req->if_unmodified_since);
    free(req->body);
    memset(req, 0, sizeof(*req));
}

void http_response_init(struct http_response *resp)
{
    memset(resp, 0, sizeof(*resp));
    resp->status = 200;
    resp->reason = "OK";
}

void http_response_set_status(struct http_response *resp, int status, const char *reason)
{
    resp->status = status;
    resp->reason = reason;
}

void http_response_set_body(struct http_response *resp, const char *content_type,
                             char *body, size_t body_len)
{
    free(resp->content_type);
    resp->content_type = content_type ? xstrdup(content_type) : NULL;
    free(resp->body);
    resp->body = body;
    resp->body_len = body_len;
}

void http_response_add_header(struct http_response *resp, const char *line)
{
    size_t old_len = resp->extra_headers ? strlen(resp->extra_headers) : 0;
    size_t add_len = strlen(line);
    char *n = realloc(resp->extra_headers, old_len + add_len + 3);
    if (!n) {
        return;
    }
    memcpy(n + old_len, line, add_len);
    n[old_len + add_len] = '\r';
    n[old_len + add_len + 1] = '\n';
    n[old_len + add_len + 2] = '\0';
    resp->extra_headers = n;
}

void http_response_free(struct http_response *resp)
{
    if (!resp) {
        return;
    }
    free(resp->content_type);
    free(resp->extra_headers);
    free(resp->body);
    memset(resp, 0, sizeof(*resp));
}

void http_response_flush(FCGX_Request *request, const struct http_response *resp, int omit_body)
{
    /* En environnement FastCGI, la premiere ligne envoyee au serveur web
     * est "Status: <code> <raison>", pas une vraie status-line HTTP/1.1 -
     * c'est le serveur web (nginx, etc.) qui la traduit. */
    FCGX_FPrintF(request->out, "Status: %d %s\r\n", resp->status,
                 resp->reason ? resp->reason : "");

    if (resp->content_type) {
        FCGX_FPrintF(request->out, "Content-Type: %s\r\n", resp->content_type);
    }
    FCGX_FPrintF(request->out, "Content-Length: %zu\r\n", resp->body_len);
    /* RFC 8040 SS5.5 : le contenu RESTCONF ne doit generalement pas etre
     * mis en cache. */
    FCGX_FPrintF(request->out, "Cache-Control: no-cache\r\n");

    if (resp->extra_headers) {
        FCGX_FPrintF(request->out, "%s", resp->extra_headers);
    }

    FCGX_FPrintF(request->out, "\r\n");

    if (!omit_body && resp->body && resp->body_len > 0) {
        FCGX_PutStr(resp->body, (int)resp->body_len, request->out);
    }
}

/* --------------------------------------------------------------------
 * Server-Sent Events (RFC 8040 SS3.8/SS6.2/SS6.3, W3C SSE)
 * -------------------------------------------------------------------- */

int http_request_wants_event_stream(const struct http_request *req)
{
    if (!req || !req->accept) {
        return 0;
    }
    /* Recherche grossiere de sous-chaine : suffisant ici puisque
     * 'text/event-stream' n'a pas de parametres pertinents pour ce
     * squelette (contrairement a la negociation JSON/XML de
     * restconf_handler.c, qui analyse la liste complete des media-ranges
     * avec leurs qualites -- pas necessaire pour un simple "le client
     * veut-il du SSE ?"). */
    return strcasestr(req->accept, "text/event-stream") != NULL;
}

void http_sse_send_headers(FCGX_Request *request)
{
    /* RFC 8040 SS6.3/SS6.4 : pas de Content-Length (corps de duree
     * indefinie) ; Cache-Control: no-cache comme pour les autres
     * reponses RESTCONF (SS5.5). 'X-Accel-Buffering: no' desactive la
     * bufferisation de reponse de nginx pour cette requete precise (a
     * defaut de le faire globalement dans la configuration du serveur
     * web, cf. etc/nginx-restconf.conf.example), indispensable pour que
     * les evenements atteignent le client au fur et a mesure plutot que
     * d'etre retenus jusqu'a la fin (qui n'arrive jamais) de la reponse. */
    FCGX_FPrintF(request->out, "Status: 200 OK\r\n");
    FCGX_FPrintF(request->out, "Content-Type: text/event-stream\r\n");
    FCGX_FPrintF(request->out, "Cache-Control: no-cache\r\n");
    FCGX_FPrintF(request->out, "X-Accel-Buffering: no\r\n");
    FCGX_FPrintF(request->out, "Connection: keep-alive\r\n");
    FCGX_FPrintF(request->out, "\r\n");
    FCGX_FFlush(request->out);
}

/* Ecrit un champ SSE multi-lignes ("data: <ligne>\n" pour chaque ligne de
 * 'value', puisque le format SSE interdit un saut de ligne litteral a
 * l'interieur d'un seul champ 'data:'). 'json_notification' etant du
 * JSON serialise sur une seule ligne par libyang (lyd_print_mem() sans
 * LYD_PRINT_FORMAT/WITHSIBLINGS), ceci degenere en pratique en une seule
 * ligne "data: {...}", mais reste correct si ce n'etait pas le cas. */
static int sse_write_field(FCGX_Request *request, const char *field, const char *value)
{
    const char *p = value;
    while (p) {
        const char *nl = strchr(p, '\n');
        int n;
        if (nl) {
            n = FCGX_FPrintF(request->out, "%s: %.*s\n", field, (int)(nl - p), p);
            p = nl + 1;
        } else {
            n = FCGX_FPrintF(request->out, "%s: %s\n", field, p);
            p = NULL;
        }
        if (n < 0) {
            return -1;
        }
    }
    return 0;
}

int http_sse_send_event(FCGX_Request *request, const char *json_notification)
{
    if (sse_write_field(request, "data", json_notification) != 0) {
        return -1;
    }
    if (FCGX_FPrintF(request->out, "\n") < 0) {
        return -1;
    }
    /* RFC 8040 SS6.4 dernier paragraphe : le champ 'retry' peut etre
     * envoye par le serveur (recommande cote client) ; ce squelette ne
     * l'envoie pas (pas de reconnexion geree cote serveur au-dela de ce
     * que FastCGI/nginx font deja par defaut). */
    return FCGX_FFlush(request->out) < 0 ? -1 : 0;
}

int http_sse_send_comment(FCGX_Request *request, const char *comment)
{
    if (FCGX_FPrintF(request->out, ": %s\n\n", comment ? comment : "keep-alive") < 0) {
        return -1;
    }
    return FCGX_FFlush(request->out) < 0 ? -1 : 0;
}
