#ifndef RESTCONFD_RESTCONF_HANDLER_H
#define RESTCONFD_RESTCONF_HANDLER_H

#include "http.h"

/* Racine {+restconf} configuree (defaut "/restconf"). */
extern const char *g_restconf_root;

/* Traite une requete deja extraite d'une requete FastCGI et remplit
 * *resp en consequence. Gere aussi bien /.well-known/host-meta que les
 * ressources sous g_restconf_root. */
void restconf_handle_request(const struct http_request *req, struct http_response *resp);

#endif /* RESTCONFD_RESTCONF_HANDLER_H */
