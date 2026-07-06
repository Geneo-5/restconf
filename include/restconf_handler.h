#ifndef RESTCONFD_RESTCONF_HANDLER_H
#define RESTCONFD_RESTCONF_HANDLER_H

#include <fcgiapp.h>

#include "http.h"

/* Racine {+restconf} configuree (defaut "/restconf"). */
extern const char *g_restconf_root;

/* Selection a l'execution du modele de gestion des flux SSE (RFC 8040
 * SS3.8/SS6) : 0 (defaut) = modele thread-per-flux historique ; 1 =
 * boucle libev partagee (cf. README.md "Migration vers libev"). N'a
 * d'effet que si restconf_ev_available() (ev_loop.h) renvoie 1, i.e. si
 * le binaire a ete construit avec -DBUILD_LIBEV=ON ; sinon ce flag est
 * silencieusement ignore et le modele thread-per-flux reste utilise.
 * Positionne par main.c depuis les arguments --sse-libev/--sse-threaded. */
extern int g_restconf_sse_use_libev;

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
