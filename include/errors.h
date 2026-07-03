#ifndef RESTCONFD_ERRORS_H
#define RESTCONFD_ERRORS_H

#include <stddef.h>

/* Une entree d'erreur au sens de la liste "errors/error" du gabarit
 * yang-data "yang-errors" (RFC 8040 SS8 et SS7.1). */
struct restconf_error {
    char *error_type;     /* "transport" | "rpc" | "protocol" | "application" */
    char *error_tag;      /* "invalid-value", "operation-not-supported", ...  */
    char *error_app_tag;  /* optionnel                                        */
    char *error_path;     /* optionnel, instance-identifier XPath              */
    char *error_message;  /* optionnel                                        */
};

/* Renvoie un code de statut HTTP par defaut pour un error-tag donne,
 * conformement au tableau de la RFC 8040 SS7. Quand plusieurs codes sont
 * possibles dans la RFC (ex: invalid-value -> 400/404/406), la valeur la
 * plus generale est renvoyee ; les appelants peuvent forcer un statut
 * different au cas par cas. */
int restconf_error_default_status(const char *error_tag);

/* Construit un message ietf-restconf:errors en JSON (RFC 8040 SS7.1 /
 * SS8, module "ietf-restconf", gabarit "yang-errors") a partir d'un
 * tableau d'erreurs. La chaine renvoyee est allouee (a liberer par
 * l'appelant). */
char *restconf_errors_to_json(const struct restconf_error *errors, size_t n);

/* Construit un tableau a une seule erreur et son JSON associe en un
 * seul appel ; pratique pour le cas courant d'une erreur unique. */
char *restconf_error_single_json(const char *error_type, const char *error_tag,
                                  const char *error_path, const char *fmt, ...);

void restconf_error_release(struct restconf_error *e);

#endif /* RESTCONFD_ERRORS_H */
