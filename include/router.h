#ifndef ROUTER_H
#define ROUTER_H

#include <stdint.h>
#include <stdbool.h>

#include "codec.h"

// Types de ressources RESTCONF (RFC 8040 Sec 3 & RFC 8527 Sec 3.1)
typedef enum {
	RC_RES_ROOT_DISCOVERY,     // /.well-known/host-meta
	RC_RES_API,                // /restconf
	RC_RES_DATA,               // /restconf/data/...
	RC_RES_DS,                 // /restconf/ds/<datastore>/... (NMDA)
	RC_RES_OPERATIONS,         // /restconf/operations/...
	RC_RES_EVENT_STREAM,       // /restconf/streams/...
	RC_RES_UNKNOWN
} rc_resource_type_t;

// Datastores NMDA supportés
typedef enum {
	RC_DS_RUNNING,
	RC_DS_OPERATIONAL,
	RC_DS_INTENDED,
	RC_DS_UNKNOWN
} rc_datastore_t;

// Structure représentant une requête RESTCONF parsée
typedef struct {
	// Méthode HTTP (GET, POST, PUT, PATCH, DELETE)
	const char *method;
	
	// Type de ressource identifié
	rc_resource_type_t res_type;
	
	// Datastore cible (si res_type == RC_RES_DS)
	rc_datastore_t datastore;
	
	// Chemin XPath généré à partir de l'URI RESTCONF (pour libyang/sysrepo)
	char *xpath;
	
	// Nom du module et de l'opération (si res_type == RC_RES_OPERATIONS)
	char *rpc_module;
	char *rpc_name;
	
	// Query Parameters (RFC 8040 Sec 4.8 & RFC 8527 Sec 3.2)
	bool with_origin;           // RFC 8527
	int depth;                  // -1 si non spécifié (unbounded)
	char *content_filter;       // config, nonconfig, all
	char *with_defaults;        // report-all, trim, explicit, report-all-tagged
	
	// Corps de la requête (pour POST/PUT/PATCH)
	const uint8_t *body;
	size_t body_len;
	
	// Identité utilisateur (extraite du JWT)
	char *username;

	/* Types de médias (RFC 8040 Sec 3.2 & 5.2) */
	media_type_t req_type;
	media_type_t accept_type;
} rc_request_t;

/**
 * @brief Parse l'URI et les headers d'une requête HTTP/2.
 * @return 0 en cas de succès, -1 si l'URI est invalide (Bad Request).
 */
int router_parse_request(const char *path,
                         const char *method,
                         const char *auth_header,
                         const char *content_type,
                         const char *accept,
                         rc_request_t *req_out);

/**
 * @brief Libère la mémoire allouée dans rc_request_t (ex: xpath, username).
 */
void router_free_request(rc_request_t *req);

#endif // ROUTER_H