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

/* Valeurs possibles du parametre de requete "with-defaults" (RFC 8040
 * SS4.8.9, table SS4.8.9 ; RFC 8527 SS3.2.1 pour le cas particulier de
 * la datastore operational). Correspondent directement aux options
 * d'impression LYD_PRINT_WD_* de libyang (cf. sysrepo_backend.c). */
enum restconf_with_defaults_mode {
    RESTCONF_WD_UNSET = 0,     /* parametre absent : bascule sur le basic-mode
                               * annonce par le serveur (RFC 8040 SS9.1.2),
                               * ici "explicit" -> LYD_PRINT_WD_EXPLICIT */
    RESTCONF_WD_REPORT_ALL,
    RESTCONF_WD_TRIM,
    RESTCONF_WD_EXPLICIT,
    RESTCONF_WD_REPORT_ALL_TAGGED,
};

struct restconf_get_options {
    enum restconf_content_mode content;
    unsigned int depth;            /* 0 = profondeur non bornée */
    const char *fields;            /* valeur brute du parametre "fields" (RFC
                                   * 8040 SS4.8.3), NULL si absent ; analysee
                                   * par sysrepo_backend.c a l'aide du schema
                                   * compile libyang (grammaire fields-expr
                                   * complete, y compris sous-selecteurs
                                   * parentheses). Pointeur non possede (duree
                                   * de vie liee a la struct http_request de
                                   * l'appelant). */
    enum restconf_with_defaults_mode with_defaults; /* RFC 8040 SS4.8.9 */
    int with_origin;               /* RFC 8527 SS3.2.2 : 1 si "with-origin"
                                   * demande. L'appelant DOIT avoir deja
                                   * verifie que la datastore ciblee est bien
                                   * "operational" (ou derivee), sinon 400
                                   * invalid-value -- cf. restconf_handler.c ;
                                   * sysrepo_backend.c l'ignore silencieusement
                                   * en dehors de operational par prudence
                                   * supplementaire. */
};

/* Connexion sysrepo partagee par tous les threads/requetes (un seul
 * sr_connect() pour tout le processus, cf. doc sysrepo : "It is safe to
 * use a single connection by multiple threads"). A initialiser une fois
 * au demarrage avec sysrepo_backend_init(), a liberer avec
 * sysrepo_backend_destroy().
 *
 * L'initialisation verifie aussi que les modules YANG RESTCONF/NMDA
 * indispensables sont presents dans le contexte sysrepo ; installez-les
 * au prealable avec sysrepoctl si cette verification echoue. */
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
 * 'options->fields' (RFC 8040 SS4.8.3), si non NULL, est analyse (grammaire
 * fields-expr complete, y compris sous-selecteurs parentheses et chemins
 * '/') puis applique par elagage de l'arbre libyang recupere, avant
 * serialisation ; une expression malformee renvoie "invalid-value".
 * 'options->with_defaults' (RFC 8040 SS4.8.9) est traduit en option
 * d'impression LYD_PRINT_WD_* de libyang. 'options->with_origin' (RFC 8527
 * SS3.2.2) demande les metadata d'origine aupres de sysrepo (uniquement
 * honore si sr_ds correspond a la datastore operational).
 *
 * Retourne 0 en cas de succes. En cas d'echec, retourne -1 et remplit
 * *err (error-tag adapte : "invalid-value" si le chemin ne correspond a
 * aucun noeud du schema ou si 'fields' est malforme, "operation-failed"
 * en cas d'erreur sysrepo). */
int sysrepo_backend_get(int sr_ds, const struct restconf_path_segment *segments,
                         size_t nsegments, const struct restconf_get_options *options,
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

/* Semantique d'ecriture RESTCONF pour une ressource de donnees (RFC 8040
 * SS4.4 POST, SS4.5 PUT, SS4.6.1 PATCH "plain patch"). Traduite, cote
 * sysrepo/libyang, en une metadata d'edition "ietf-netconf:operation"
 * (create/replace/merge) posee sur le(s) noeud(s) apporte(s) par le corps
 * JSON de la requete avant soumission via sr_edit_batch()+sr_apply_changes(). */
enum restconf_write_op {
    RESTCONF_WRITE_CREATE,  /* POST  : le noeud cible ne doit pas deja exister (409 data-exists sinon) */
    RESTCONF_WRITE_REPLACE, /* PUT   : remplace le noeud cible, le cree s'il est absent */
    RESTCONF_WRITE_MERGE,   /* PATCH : fusionne avec le contenu existant ; le PARENT doit deja exister */
};

/* Ecrit 'body_json'/'body_len' (corps de requete RFC 7951) dans le
 * datastore sr_ds a l'emplacement designe par 'segments'/'nsegments' :
 *
 * - RESTCONF_WRITE_CREATE (POST, "create resource mode", RFC 8040
 *   SS4.4.1) : 'segments' designe la ressource PARENTE, cible de l'URI
 *   POST ; le corps doit contenir exactement une instance du nouveau
 *   noeud enfant a creer.
 * - RESTCONF_WRITE_REPLACE / RESTCONF_WRITE_MERGE (PUT SS4.5 / PATCH
 *   SS4.6.1) : 'segments' designe la ressource CIBLE elle-meme ('segments'
 *   doit donc contenir au moins un element) ; le corps represente cette
 *   ressource.
 *
 * *created_out (si non NULL) est positionne a 1 si la ressource ciblee par
 * un PUT n'existait pas avant l'operation, pour permettre a l'appelant de
 * distinguer 201 Created de 204 No Content (RFC 8040 SS4.5) ; ignore pour
 * les autres operations.
 *
 * *created_child_segment_out (si non NULL, uniquement pertinent pour
 * RESTCONF_WRITE_CREATE) recoit le segment de chemin RESTCONF
 * ("module:nom" ou "module:nom=cle1,cle2") du noeud nouvellement cree, a
 * concatener par l'appelant a la ressource parente pour construire
 * l'en-tete "Location" exige par la RFC 8040 SS4.4.1. Chaine allouee, a
 * liberer par l'appelant ; peut rester NULL si elle n'a pas pu etre
 * determinee.
 *
 * Retourne 0 en cas de succes. En cas d'echec, retourne -1 et remplit
 * *err : error-tag "malformed-message" si le corps n'est pas du JSON
 * valide ou ne correspond pas au schema attendu a cet emplacement,
 * "data-exists"/"data-missing"/"lock-denied"/"access-denied" si sysrepo
 * signale respectivement SR_ERR_EXISTS/SR_ERR_NOT_FOUND/SR_ERR_LOCKED/
 * SR_ERR_UNAUTHORIZED, "operation-failed" sinon. */
int sysrepo_backend_write(int sr_ds, const struct restconf_path_segment *segments,
                          size_t nsegments, enum restconf_write_op op, const char *body_json,
                          size_t body_len, int *created_out, char **created_child_segment_out,
                          struct restconf_error *err);

/* Supprime la ressource de donnees designee par 'segments'/'nsegments'
 * (RFC 8040 SS4.7 DELETE). 'nsegments' doit etre > 0 : la RFC ne definit
 * pas de semantique DELETE sur la racine d'une datastore. Retourne 0 en
 * cas de succes ; -1 + *err sinon ("invalid-value" si la ressource
 * n'existe pas -> l'appelant doit renvoyer 404 ; "lock-denied"/
 * "access-denied" le cas echeant ; "operation-failed" sinon). */
int sysrepo_backend_delete(int sr_ds, const struct restconf_path_segment *segments,
                           size_t nsegments, struct restconf_error *err);

/* Invoque l'operation RPC RESTCONF designee par 'segments'/'nsegments'
 * (RFC 8040 SS3.6/SS4.4.2) via sr_rpc_send_tree(). 'segments' doit
 * decrire exactement le chemin {+restconf}/operations/<module>:<rpc>,
 * c.-a-d. un unique segment qualifie par un module et sans cle
 * ('nsegments' != 1 ou segment avec cles -> "invalid-value").
 *
 * 'body_json'/'body_len' est le corps de requete optionnel (RFC 8040
 * SS3.6.1, enveloppe "module:input") ; peut etre NULL/0 si le "rpc" n'a
 * pas de section input mandatory. Notez que ceci ne gere que les RPC de
 * haut niveau (statement YANG "rpc"), pas les actions ("action",
 * invoquees via {+restconf}/data/<...>/<action>, RFC 8040 SS3.6) : ce
 * squelette ne route pas encore les actions, cf. feuille de route.
 *
 * En cas de succes, *json_out recoit soit NULL (pas de section "output"
 * -> l'appelant doit repondre 204 No Content, RFC 8040 SS4.4.2), soit le
 * corps JSON "module:output" deja enveloppe (alloue, RFC 8040 SS3.6.2)
 * -> l'appelant doit repondre 200 OK avec ce corps.
 *
 * Retourne 0 en cas de succes. En cas d'echec, retourne -1 et remplit
 * *err ("invalid-value" si le chemin ne designe pas une operation RPC
 * connue ou si le corps ne correspond pas au schema "input" attendu,
 * "access-denied"/"operation-failed" selon le code sysrepo). */
int sysrepo_backend_rpc_invoke(const struct restconf_path_segment *segments, size_t nsegments,
                               const char *body_json, size_t body_len, char **json_out,
                               struct restconf_error *err);

/* Indique si 'segments'/'nsegments' (>= 2) designe une action YANG
 * (statement "action", RFC 8040 SS3.6) : le dernier segment doit
 * resoudre, dans le schema compile libyang, vers un noeud de type
 * LYS_ACTION plutot qu'un noeud de donnees ordinaire. Utilise par
 * restconf_handler.c pour distinguer, sur un POST recu sous
 * {+restconf}/data/<...>/<dernier-segment> ou {+restconf}/ds/<n>/<...>,
 * une invocation d'action d'une creation de ressource de donnees
 * classique (RFC 8040 SS4.4.1).
 *
 * Fonction de simple lecture de schema (pas de session sysrepo) :
 * renvoie 0 (faux) sans remplir d'erreur si le chemin ne resout pas ou
 * ne designe pas une action -- l'appelant doit alors traiter la requete
 * normalement (la vraie erreur, le cas echeant, remontera via le chemin
 * normal, ex. sysrepo_backend_write()). */
int sysrepo_backend_is_action_path(const struct restconf_path_segment *segments, size_t nsegments);

/* Invoque l'action YANG (RFC 8040 SS3.6) designee par 'segments'/
 * 'nsegments' via sr_rpc_send_tree(). A la difference d'un RPC de haut
 * niveau, 'segments' decrit ici le chemin COMPLET jusqu'a l'action,
 * ancetres inclus avec leurs eventuelles valeurs de cle (ex.
 * "interfaces", "interface=eth0", "reset") ; 'nsegments' doit donc etre
 * >= 2 (au moins un ancetre + le segment d'action lui-meme).
 *
 * RFC 8527 SS3.1 : les actions ne peuvent etre invoquees que sous
 * {+restconf}/ds/ietf-datastores:operational -- cette contrainte est
 * appliquee par l'appelant (restconf_handler.c), pas par cette fonction,
 * qui se contente d'invoquer l'action au chemin donne quel que soit le
 * datastore d'origine de l'appel.
 *
 * 'body_json'/'body_len', *json_out et les codes de retour suivent
 * exactement la meme convention que sysrepo_backend_rpc_invoke() (RFC
 * 8040 SS3.6.1/SS3.6.2/SS4.4.2). */
int sysrepo_backend_action_invoke(const struct restconf_path_segment *segments, size_t nsegments,
                                  const char *body_json, size_t body_len, char **json_out,
                                  struct restconf_error *err);

#endif /* RESTCONFD_SYSREPO_BACKEND_H */
