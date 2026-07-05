#ifndef RESTCONFD_RESTCONF_PATH_H
#define RESTCONFD_RESTCONF_PATH_H

#include <stddef.h>
#include "errors.h"

/* Un segment de "api-path" (RFC 8040 SS3.5.3.1) : un noeud eventuellement
 * qualifie par un nom de module, avec ses valeurs de cle si c'est une
 * instance de liste/leaf-list ("list1=key1,key2,key3" ou "Y=val"). Les
 * valeurs sont deja percent-decodees. */
struct restconf_path_segment {
    char *module;  /* NULL si le module est herite du segment precedent */
    char *name;
    char **keys;
    size_t nkeys;
};

enum restconf_resource_type {
    RESTCONF_RES_ROOT,                 /* {+restconf}                         */
    RESTCONF_RES_HOST_META,            /* /.well-known/host-meta               */
    RESTCONF_RES_DATA,                 /* {+restconf}/data[/<api-path>]        */
    RESTCONF_RES_DATASTORE,            /* {+restconf}/ds/<name>[/<api-path>]   */
    RESTCONF_RES_OPERATIONS,           /* {+restconf}/operations[/<api-path>]  */
    RESTCONF_RES_YANG_LIBRARY_VERSION, /* {+restconf}/yang-library-version     */
    RESTCONF_RES_STREAMS,              /* {+restconf}/streams/<stream-name>    */
    RESTCONF_RES_UNKNOWN,
};

struct restconf_request_path {
    enum restconf_resource_type type;

    /* Pour RESTCONF_RES_DATASTORE : identityref brut tel que recu dans
     * l'URI, ex. "ietf-datastores:operational". */
    char *datastore_identityref;

    /* Pour RESTCONF_RES_STREAMS : nom du flux demande (RFC 8040 SS3.8),
     * deja percent-decode. Ce squelette fait correspondre un nom de flux
     * directement a un nom de module YANG portant des notifications (cf.
     * sysrepo_backend_stream_subscribe()) ; ce n'est PAS un "api-path" au
     * sens SS3.5.3.1 (pas de segments/cles), d'ou ce champ dedie plutot
     * que 'segments'. NULL si {+restconf}/streams est demande sans nom de
     * flux (-> 400 invalid-value, cf. restconf_path_parse()). */
    char *stream_name;

    struct restconf_path_segment *segments;
    size_t nsegments;
};

/* Analyse un chemin HTTP encore percent-encode (PATH_INFO, sans query
 * string) en RESTCONF_RES_* + segments. 'restconf_root' est la racine
 * {+restconf} configuree (ex. "/restconf") ; elle est retiree du debut
 * de raw_path si presente (tolerance si le serveur web ne le fait pas
 * deja via fastcgi_split_path_info).
 *
 * Retourne 0 en cas de succes. En cas d'erreur de grammaire, retourne -1
 * et remplit *err (error-tag "invalid-value", cf. RFC 8040 SS3.5.3.1). */
int restconf_path_parse(const char *restconf_root, const char *raw_path,
                         struct restconf_request_path *out, struct restconf_error *err);

void restconf_path_free(struct restconf_request_path *p);

#endif /* RESTCONFD_RESTCONF_PATH_H */
