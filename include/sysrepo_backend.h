#ifndef RESTCONFD_SYSREPO_BACKEND_H
#define RESTCONFD_SYSREPO_BACKEND_H

#include <stddef.h>
#include "errors.h"
#include "restconf_path.h"

/* Valeurs possibles du parametre de requete "content" (RFC 8040 SS4.8.1). */
enum restconf_content_mode {
    RESTCONF_CONTENT_ALL = 0,
    RESTCONF_CONTENT_CONFIG,
    RESTCONF_CONTENT_NONCONFIG,
};

/* Connexion sysrepo partagee par tous les threads/requetes (un seul
 * sr_connect() pour tout le processus, cf. doc sysrepo : "It is safe to
 * use a single connection by multiple threads"). A initialiser une fois
 * au demarrage avec sysrepo_backend_init(), a liberer avec
 * sysrepo_backend_destroy(). */
int sysrepo_backend_init(void);
void sysrepo_backend_destroy(void);

/* Traduit un identityref RFC 8527 ("ietf-datastores:running", ...) vers
 * le sr_datastore_t sysrepo correspondant. Retourne 0 en cas de succes ;
 * -1 si le datastore est inconnu ou non gere par ce serveur (l'appelant
 * doit alors renvoyer une erreur "invalid-value"/400, cf. RFC 8527
 * SS3.2.2), avec *read_only positionne a 1 si le datastore est connu
 * mais intrinsequement en lecture seule (ex. <intended>, RFC 8527
 * SS3.2, 3e tiret -> 405 operation-not-supported en ecriture). */
int sysrepo_backend_datastore_from_identityref(const char *identityref, int *sr_ds_out,
                                                int *read_only);

/* Recupere le sous-arbre RESTCONF designe par 'segments' dans le
 * datastore sr_ds, le serialise en JSON (application/yang-data+json,
 * RFC 7951) et le renvoie dans *json_out (alloue). 'root_wrapper' est
 * le nom a utiliser pour le noeud racine synthetique quand plusieurs
 * noeuds de haut niveau doivent etre combines (ex. "data" pour
 * {+restconf}/data lui-meme) ; peut etre NULL si la cible est un noeud
 * unique bien identifie.
 *
 * Retourne 0 en cas de succes. En cas d'echec, retourne -1 et remplit
 * *err (error-tag adapte : "invalid-value" si le chemin ne correspond a
 * aucun noeud du schema, "operation-failed" en cas d'erreur sysrepo). */
int sysrepo_backend_get(int sr_ds, const struct restconf_path_segment *segments,
                         size_t nsegments, enum restconf_content_mode content,
                         char **json_out, struct restconf_error *err);

/* Construit le contenu JSON de {+restconf}/yang-library-version (RFC
 * 8040 SS3.3.3), a partir de la revision du module ietf-yang-library
 * effectivement chargee par sysrepo. */
int sysrepo_backend_yang_library_version(char **json_out, struct restconf_error *err);

/* Variante ne renvoyant que la valeur de revision brute (non enveloppee
 * en JSON), utile pour la composer dans d'autres reponses (ex. la
 * ressource API racine). */
int sysrepo_backend_get_yang_library_revision(char **revision_out, struct restconf_error *err);

/* Datastore sysrepo utilisee pour {+restconf}/data (sans passer par
 * {+restconf}/ds/<name>). RFC 8040 (pre-NMDA) definit cette ressource
 * comme la vue combinee configuration+etat ; par simplicite ce squelette
 * la fait correspondre a la datastore <running> de sysrepo (donnees de
 * configuration uniquement). A ajuster si vous avez besoin d'une vue
 * fusionnee avec l'etat operationnel. Renvoie une valeur castable en
 * sr_datastore_t. */
int sysrepo_backend_default_data_datastore(void);


#endif /* RESTCONFD_SYSREPO_BACKEND_H */
