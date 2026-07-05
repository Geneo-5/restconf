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

    /* Positionne a 1 par restconf_handle_request() quand la reponse a
     * deja ete entierement ecrite directement sur le FCGX_Request (cas
     * d'un flux SSE, RFC 8040 SS3.8/SS6 : en-tetes + evenements envoyes
     * au fil de l'eau, pas de corps unique a bufferiser dans 'body'). Le
     * boucle principale (main.c) DOIT alors sauter l'appel a
     * http_response_flush() pour cette requete. */
    int already_sent;
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

/* --------------------------------------------------------------------
 * Server-Sent Events (RFC 8040 SS3.8/SS6.2/SS6.3, W3C SSE)
 * -------------------------------------------------------------------- */

/* Indique si le client demande un flux SSE via l'en-tete 'Accept' (media
 * type "text/event-stream", RFC 8040 SS6.3). */
int http_request_wants_event_stream(const struct http_request *req);

/* Envoie directement sur 'request' les en-tetes HTTP d'un flux SSE (RFC
 * 8040 SS6.3/6.4). N'ecrit PAS de corps : chaque evenement est ensuite
 * envoye separement via http_sse_send_event()/http_sse_send_comment(). */
void http_sse_send_headers(FCGX_Request *request);

/* Envoie un evenement SSE dont le champ 'data' est 'json_notification'
 * suivi d'une ligne vide (RFC 8040 SS6.4). Renvoie 0 en cas de succes,
 * -1 si l'ecriture a echoue (client deconnecte). */
int http_sse_send_event(FCGX_Request *request, const char *json_notification);

/* Envoie une ligne de commentaire SSE (heartbeat), memes conventions de
 * retour que http_sse_send_event(). */
int http_sse_send_comment(FCGX_Request *request, const char *comment);

#endif /* RESTCONFD_HTTP_H */
