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
