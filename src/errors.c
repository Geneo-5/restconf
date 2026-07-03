#include "errors.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Table "Mapping from <error-tag> to Status Code", RFC 8040 SS7, page 74.
 * Lorsque la RFC propose plusieurs codes possibles, on retient celui qui
 * correspond au cas le plus general (le code precis - 404 au lieu de 400,
 * par exemple - doit etre positionne explicitement par l'appelant quand
 * le contexte le justifie). */
struct tag_status {
    const char *tag;
    int status;
};

static const struct tag_status TAG_TABLE[] = {
    { "in-use", 409 },
    { "invalid-value", 400 },
    { "too-big", 413 },
    { "missing-attribute", 400 },
    { "bad-attribute", 400 },
    { "unknown-attribute", 400 },
    { "bad-element", 400 },
    { "unknown-element", 400 },
    { "unknown-namespace", 400 },
    { "access-denied", 403 },
    { "lock-denied", 409 },
    { "resource-denied", 409 },
    { "rollback-failed", 500 },
    { "data-exists", 409 },
    { "data-missing", 409 },
    { "operation-not-supported", 405 },
    { "operation-failed", 500 },
    { "partial-operation", 500 },
    { "malformed-message", 400 },
};

int restconf_error_default_status(const char *error_tag)
{
    if (!error_tag) {
        return 500;
    }
    for (size_t i = 0; i < sizeof(TAG_TABLE) / sizeof(TAG_TABLE[0]); i++) {
        if (strcmp(TAG_TABLE[i].tag, error_tag) == 0) {
            return TAG_TABLE[i].status;
        }
    }
    return 500;
}

/* Echappe une chaine pour une insertion litterale dans du JSON. */
static char *json_escape(const char *s)
{
    if (!s) {
        return strdup("");
    }
    size_t len = strlen(s);
    /* Majoration large : chaque octet peut devenir \u00XX (6 caracteres). */
    char *out = malloc(len * 6 + 1);
    if (!out) {
        return NULL;
    }
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"': out[o++] = '\\'; out[o++] = '"'; break;
        case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
        case '\n': out[o++] = '\\'; out[o++] = 'n'; break;
        case '\r': out[o++] = '\\'; out[o++] = 'r'; break;
        case '\t': out[o++] = '\\'; out[o++] = 't'; break;
        default:
            if (c < 0x20) {
                o += (size_t)sprintf(out + o, "\\u%04x", c);
            } else {
                out[o++] = (char)c;
            }
        }
    }
    out[o] = '\0';
    return out;
}

char *restconf_errors_to_json(const struct restconf_error *errors, size_t n)
{
    /* Buffer dynamique simple (les messages d'erreur restent courts). */
    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        return NULL;
    }

#define APPEND(...)                                                          \
    do {                                                                     \
        int need = snprintf(NULL, 0, __VA_ARGS__);                           \
        if (need < 0) {                                                      \
            break;                                                           \
        }                                                                    \
        while (len + (size_t)need + 1 > cap) {                               \
            cap *= 2;                                                        \
            char *nb = realloc(buf, cap);                                    \
            if (!nb) {                                                       \
                free(buf);                                                   \
                return NULL;                                                 \
            }                                                                \
            buf = nb;                                                        \
        }                                                                    \
        len += (size_t)sprintf(buf + len, __VA_ARGS__);                      \
    } while (0)

    APPEND("{\"ietf-restconf:errors\":{\"error\":[");
    for (size_t i = 0; i < n; i++) {
        const struct restconf_error *e = &errors[i];
        char *type = json_escape(e->error_type ? e->error_type : "application");
        char *tag = json_escape(e->error_tag ? e->error_tag : "operation-failed");

        APPEND("%s{\"error-type\":\"%s\",\"error-tag\":\"%s\"", i ? "," : "", type, tag);
        free(type);
        free(tag);

        if (e->error_app_tag) {
            char *v = json_escape(e->error_app_tag);
            APPEND(",\"error-app-tag\":\"%s\"", v);
            free(v);
        }
        if (e->error_path) {
            char *v = json_escape(e->error_path);
            APPEND(",\"error-path\":\"%s\"", v);
            free(v);
        }
        if (e->error_message) {
            char *v = json_escape(e->error_message);
            APPEND(",\"error-message\":\"%s\"", v);
            free(v);
        }
        APPEND("}");
    }
    APPEND("]}}");

#undef APPEND
    return buf;
}

char *restconf_error_single_json(const char *error_type, const char *error_tag,
                                  const char *error_path, const char *fmt, ...)
{
    struct restconf_error e;
    memset(&e, 0, sizeof(e));
    e.error_type = (char *)error_type;
    e.error_tag = (char *)error_tag;
    e.error_path = (char *)error_path;

    char *message = NULL;
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        int need = vsnprintf(NULL, 0, fmt, ap);
        va_end(ap);
        if (need >= 0) {
            message = malloc((size_t)need + 1);
            if (message) {
                va_start(ap, fmt);
                vsnprintf(message, (size_t)need + 1, fmt, ap);
                va_end(ap);
            }
        }
    }
    e.error_message = message;

    char *json = restconf_errors_to_json(&e, 1);
    free(message);
    return json;
}

void restconf_error_release(struct restconf_error *e)
{
    if (!e) {
        return;
    }
    free(e->error_type);
    free(e->error_tag);
    free(e->error_app_tag);
    free(e->error_path);
    free(e->error_message);
    memset(e, 0, sizeof(*e));
}
