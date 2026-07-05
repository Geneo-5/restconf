#include "restconf_path.h"
#include "http.h"

#include <stdlib.h>
#include <string.h>

static void set_invalid_value(struct restconf_error *err, const char *fmt_msg)
{
    if (!err) {
        return;
    }
    err->error_type = strdup("protocol");
    err->error_tag = strdup("invalid-value");
    err->error_message = fmt_msg ? strdup(fmt_msg) : NULL;
}

/* Parse un unique segment d'api-path (deja isole entre deux '/' litteraux,
 * ENCORE percent-encode) en {module, name, keys[]}.
 *
 * Grammaire (RFC 8040 SS3.5.3.1) :
 *   api-identifier = [module-name ":"] identifier
 *   list-instance  = api-identifier "=" key-value *("," key-value)
 */
static int parse_one_segment(const char *tok, size_t tok_len, struct restconf_path_segment *seg,
                              struct restconf_error *err)
{
    memset(seg, 0, sizeof(*seg));

    const char *eq = memchr(tok, '=', tok_len);
    size_t name_part_len = eq ? (size_t)(eq - tok) : tok_len;

    const char *colon = memchr(tok, ':', name_part_len);
    if (colon) {
        seg->module = http_percent_decode(tok, (size_t)(colon - tok));
        size_t name_off = (size_t)(colon - tok) + 1;
        seg->name = http_percent_decode(tok + name_off, name_part_len - name_off);
    } else {
        seg->module = NULL;
        seg->name = http_percent_decode(tok, name_part_len);
    }

    if (!seg->name || !*seg->name) {
        set_invalid_value(err, "segment de chemin RESTCONF vide ou mal forme");
        return -1;
    }

    if (eq) {
        const char *keys_start = eq + 1;
        size_t keys_len = tok_len - name_part_len - 1;

        /* Compte les valeurs de cle (separees par des virgules litterales ;
         * une virgule appartenant a une valeur doit etre encodee %2C par le
         * client, cf. RFC 8040 SS3.5.3). */
        size_t count = 1;
        for (size_t i = 0; i < keys_len; i++) {
            if (keys_start[i] == ',') {
                count++;
            }
        }
        seg->keys = calloc(count, sizeof(char *));
        if (!seg->keys) {
            return -1;
        }
        size_t idx = 0;
        const char *p = keys_start;
        const char *end = keys_start + keys_len;
        while (p <= end) {
            const char *comma = memchr(p, ',', (size_t)(end - p));
            size_t vlen = comma ? (size_t)(comma - p) : (size_t)(end - p);
            seg->keys[idx++] = http_percent_decode(p, vlen);
            if (!comma) {
                break;
            }
            p = comma + 1;
        }
        seg->nkeys = idx;
    }

    return 0;
}

/* Decoupe 'path' (encore percent-encode) sur les '/' litteraux et remplit
 * *segments/*nsegments. Les segments vides (chemin commencant/finissant
 * par '/') sont ignores. */
static int parse_api_path(const char *path, struct restconf_path_segment **segments,
                           size_t *nsegments, struct restconf_error *err)
{
    *segments = NULL;
    *nsegments = 0;
    if (!path || !*path) {
        return 0;
    }

    size_t cap = 4;
    struct restconf_path_segment *segs = calloc(cap, sizeof(*segs));
    if (!segs) {
        return -1;
    }
    size_t n = 0;

    const char *p = path;
    while (*p == '/') {
        p++;
    }
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t tok_len = slash ? (size_t)(slash - p) : strlen(p);
        if (tok_len > 0) {
            if (n == cap) {
                cap *= 2;
                struct restconf_path_segment *ns = realloc(segs, cap * sizeof(*segs));
                if (!ns) {
                    free(segs);
                    return -1;
                }
                segs = ns;
            }
            if (parse_one_segment(p, tok_len, &segs[n], err) != 0) {
                for (size_t i = 0; i < n; i++) {
                    free(segs[i].module);
                    free(segs[i].name);
                    for (size_t k = 0; k < segs[i].nkeys; k++) {
                        free(segs[i].keys[k]);
                    }
                    free(segs[i].keys);
                }
                free(segs);
                return -1;
            }
            n++;
        }
        if (!slash) {
            break;
        }
        p = slash + 1;
    }

    *segments = segs;
    *nsegments = n;
    return 0;
}

int restconf_path_parse(const char *restconf_root, const char *raw_path,
                         struct restconf_request_path *out, struct restconf_error *err)
{
    memset(out, 0, sizeof(*out));

    const char *p = raw_path ? raw_path : "/";

    /* Tolerance : si le serveur web n'a pas deja retire la racine
     * {+restconf} (cas normal avec fastcgi_split_path_info), on la
     * retire ici. */
    size_t root_len = restconf_root ? strlen(restconf_root) : 0;
    if (root_len > 0 && strncmp(p, restconf_root, root_len) == 0) {
        p += root_len;
    }

    /* Chemin vide ou "/" seul -> ressource API racine. */
    if (*p == '\0' || strcmp(p, "/") == 0) {
        out->type = RESTCONF_RES_ROOT;
        return 0;
    }

    if (*p == '/') {
        p++;
    }

    const char *slash = strchr(p, '/');
    size_t first_len = slash ? (size_t)(slash - p) : strlen(p);
    const char *rest = slash ? slash + 1 : "";

    if (first_len == 4 && strncmp(p, "data", 4) == 0) {
        out->type = RESTCONF_RES_DATA;
        return parse_api_path(rest, &out->segments, &out->nsegments, err);
    }

    if (first_len == 2 && strncmp(p, "ds", 2) == 0) {
        if (!*rest) {
            set_invalid_value(err, "'/restconf/ds' necessite un identityref de datastore");
            return -1;
        }
        const char *slash2 = strchr(rest, '/');
        size_t ds_len = slash2 ? (size_t)(slash2 - rest) : strlen(rest);
        out->datastore_identityref = http_percent_decode(rest, ds_len);
        out->type = RESTCONF_RES_DATASTORE;
        const char *rest2 = slash2 ? slash2 + 1 : "";
        return parse_api_path(rest2, &out->segments, &out->nsegments, err);
    }

    if (first_len == 10 && strncmp(p, "operations", 10) == 0) {
        out->type = RESTCONF_RES_OPERATIONS;
        return parse_api_path(rest, &out->segments, &out->nsegments, err);
    }

    if (first_len == 20 && strncmp(p, "yang-library-version", 20) == 0) {
        out->type = RESTCONF_RES_YANG_LIBRARY_VERSION;
        return 0;
    }

    if (first_len == 7 && strncmp(p, "streams", 7) == 0) {
        /* RFC 8040 SS3.8 : un flux d'evenements n'est pas une ressource de
         * donnees YANG (pas de grammaire api-path/predicats de cle) -- on
         * se contente donc de decoder le segment suivant tel quel comme
         * nom de flux, sans passer par parse_api_path(). Un segment
         * supplementaire ('rest' non vide au-dela du nom) est rejete. */
        out->type = RESTCONF_RES_STREAMS;
        if (!*rest) {
            out->stream_name = NULL;
            return 0;
        }
        const char *slash2 = strchr(rest, '/');
        size_t name_len = slash2 ? (size_t)(slash2 - rest) : strlen(rest);
        if (slash2 && *(slash2 + 1)) {
            set_invalid_value(err, "'/restconf/streams/<nom>' ne prend pas de sous-chemin");
            return -1;
        }
        out->stream_name = http_percent_decode(rest, name_len);
        return 0;
    }

    set_invalid_value(err, "ressource RESTCONF inconnue");
    out->type = RESTCONF_RES_UNKNOWN;
    return -1;
}

void restconf_path_free(struct restconf_request_path *p)
{
    if (!p) {
        return;
    }
    free(p->datastore_identityref);
    free(p->stream_name);
    for (size_t i = 0; i < p->nsegments; i++) {
        free(p->segments[i].module);
        free(p->segments[i].name);
        for (size_t k = 0; k < p->segments[i].nkeys; k++) {
            free(p->segments[i].keys[k]);
        }
        free(p->segments[i].keys);
    }
    free(p->segments);
    memset(p, 0, sizeof(*p));
}
