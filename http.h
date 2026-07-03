#ifndef RESTCONFD_HTTP_H
#define RESTCONFD_HTTP_H

#include <stddef.h>
#include <fcgiapp.h>

/* Une paire nom/valeur de parametre de requete (query string), deja
 * percent-decodee. */
struct http_query_param {
    char *name;
    char *value;
};

/* Requete HTTP telle qu'extraite d'un FCGX_Request. Tous les champs
 * sont soit NULL, soit des chaines allouees sur le tas (a liberer avec
 * http_request_free()). */
struct http_request {
    char *method;          /* "GET", "POST", ... */
    char *raw_path;        /* PATH_INFO / SCRIPT_NAME, non decode   */
    char *path;             /* chemin percent-decode                 */
    char *query_string;     /* QUERY_STRING brute                    */
    struct http_query_param *params;
    size_t nparams;

    char *content_type;
    char *accept;
    char *if_match;
    char *if_unmodified_since;

    char *body;              /* corps brut (peut etre NULL)          */
    size_t body_len;
};

/* Reponse HTTP en cours de construction. */
struct http_response {
    int status;               /* 200, 404, ...                        */
    const char *reason;       /* "OK", "Not Found", ...                */
    char *content_type;       /* alloue, ou NULL                       */
    char *extra_headers;      /* lignes "Nom: valeur\r\n" supplementaires, ou NULL */
    char *body;                /* alloue, ou NULL                       */
    size_t body_len;
};

/* Construit une struct http_request a partir d'une requete FastCGI en
 * cours (request.envp / request.in). Retourne 0 en cas de succes. */
int http_request_from_fcgx(FCGX_Request *request, struct http_request *out);
void http_request_free(struct http_request *req);

/* Recherche un parametre de query string (retourne NULL si absent). */
const char *http_request_get_param(const struct http_request *req, const char *name);

void http_response_init(struct http_response *resp);
void http_response_set_status(struct http_response *resp, int status, const char *reason);
void http_response_set_body(struct http_response *resp, const char *content_type,
                             char *body /* prend possession */, size_t body_len);
void http_response_add_header(struct http_response *resp, const char *line /* "Nom: valeur" */);
void http_response_free(struct http_response *resp);

/* Ecrit la ligne de statut, les en-tetes et (sauf si omit_body est vrai,
 * cas de la methode HEAD, RFC 8040 SS4.2) le corps sur request.out. */
void http_response_flush(FCGX_Request *request, const struct http_response *resp, int omit_body);

/* Decode percent-encoding (RFC 3986 section 2.1) dans une chaine C.
 * Le resultat est alloue et doit etre libere par l'appelant. '+' n'est
 * PAS traite comme un espace (ce n'est pas une query "x-www-form"). */
char *http_percent_decode(const char *s, size_t len);

#endif /* RESTCONFD_HTTP_H */
