#ifndef RESTCONFD_RESTCONF_HANDLER_H
#define RESTCONFD_RESTCONF_HANDLER_H

#include <fcgiapp.h>

#include "http.h"

/* Racine {+restconf} configuree (defaut "/restconf"). */
extern const char *g_restconf_root;

/* Traite une requete deja extraite d'une requete FastCGI et remplit
 * *resp en consequence. Gere aussi bien /.well-known/host-meta que les
 * ressources sous g_restconf_root.
 *
 * 'fcgx_request' est la requete FastCGI BRUTE sous-jacente (celle-la
 * meme dont 'req' a ete extrait, cf. http_request_from_fcgx()) : elle
 * n'est utilisee QUE pour {+restconf}/streams/<nom> avec
 * 'Accept: text/event-stream' (RFC 8040 SS3.8/SS6), ou la reponse est un
 * flux d'evenements de duree indefinie ecrit directement dessus plutot
 * que bufferise dans '*resp' -- dans ce cas 'resp->already_sent' est mis
 * a 1 et l'appelant (main.c) NE DOIT PAS appeler http_response_flush().
 * Pour toute autre ressource, 'fcgx_request' n'est pas deref. */
void restconf_handle_request(FCGX_Request *fcgx_request, const struct http_request *req,
                              struct http_response *resp);

#endif /* RESTCONFD_RESTCONF_HANDLER_H */
