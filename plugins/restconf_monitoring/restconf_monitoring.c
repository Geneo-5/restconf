/**
 * @file restconf_monitoring.c
 * @brief Plugin sysrepo fournissant ietf-restconf-monitoring:restconf-state
 *        comme donnee operationnelle (sr_oper_get_subscribe()), au lieu du
 *        JSON construit a la main auparavant dans restconfd
 *        (src/restconf_handler.c, cf. feuille de route "Plugin sysrepo"
 *        dans README.md, priorite haute).
 *
 * Ce plugin est destine a etre charge par sysrepo-plugind (comme l'exemple
 * "oven" fourni avec sysrepo, cf. AGENTS.md/CLAUDE.md) : il tourne dans un
 * processus SEPARE de restconfd. restconfd n'a plus besoin de connaitre le
 * contenu de restconf-state : il se contente de lire
 * {+restconf}/ds/ietf-datastores:operational (ou {+restconf}/data, redirige
 * vers <operational> pour ce sous-arbre precis, cf.
 * handle_restconf_monitoring_state() dans src/restconf_handler.c) comme
 * n'importe quelle autre ressource de donnees, via sysrepo_backend_get().
 *
 * Installation : copier le .so resultant (cible CMake
 * "restconf_monitoring_plugin") dans le repertoire affiche par cmake comme
 * "SRPD plugins path" lors de la configuration de sysrepo, puis
 * (re)demarrer sysrepo-plugind. Cf. la documentation sysrepo, section
 * "Developer Guide / Plugin Example" (meme mecanisme que le plugin
 * d'exemple "oven").
 *
 * Limite actuelle : seul le conteneur "capabilities" (RFC 8040 SS9.1) est
 * fourni ; "streams" (RFC 8040 SS9.2) reste a faire (cf. feuille de route
 * "Notifications" dans README.md, dont il depend logiquement -- un stream
 * annonce sans mecanisme de livraison SSE derriere n'aurait pas de sens).
 *
 * Non teste contre un sysrepo/libyang reels (pas d'acces reseau pour les
 * installer dans cet environnement, cf. "Points sensibles a la version
 * installee" dans README.md) : verifiez sr_oper_get_subscribe() et
 * lyd_new_path() contre les en-tetes que vous avez effectivement compiles.
 */

#include <stdint.h>
#include <string.h>

#include <libyang/libyang.h>
#include <sysrepo.h>

/* Contexte de subscription, valide jusqu'a sr_plugin_cleanup_cb(). */
static sr_subscription_ctx_t *g_sub;

/* Capacites RESTCONF annoncees (RFC 8040 SS9.1.1, table ; RFC 8527
 * SS3.2.1/SS3.2.2 pour with-operational-defaults/with-origin). Reprend
 * exactement la liste auparavant codee en dur dans l'ancien
 * handle_restconf_monitoring_capabilities() de src/restconf_handler.c
 * (desormais supprime) : n'annoncer que ce qui est reellement applique
 * par sysrepo_backend_get() (cf. "Parametres de requete" dans README.md).
 * filter/start-time/stop-time/insert/point restent non implementes cote
 * restconfd et donc volontairement absents d'ici. A tenir a jour
 * manuellement si restconfd gagne d'autres parametres de requete -- ce
 * couplage manuel entre les deux binaires (restconfd et ce plugin) est une
 * limite assumee de cette premiere version. */
static const char *const CAPABILITY_URIS[] = {
    "urn:ietf:params:restconf:capability:defaults:1.0?basic-mode=explicit",
    "urn:ietf:params:restconf:capability:depth:1.0",
    "urn:ietf:params:restconf:capability:fields:1.0",
    "urn:ietf:params:restconf:capability:with-defaults:1.0",
    "urn:ietf:params:restconf:capability:with-operational-defaults:1.0",
    "urn:ietf:params:restconf:capability:with-origin:1.0",
};

#define NCAPABILITY_URIS (sizeof(CAPABILITY_URIS) / sizeof(CAPABILITY_URIS[0]))

/* Callback fournisseur de donnees operationnelles (sr_oper_get_cb, meme
 * signature que l'exemple oven_state_cb() dans AGENTS.md/CLAUDE.md) pour
 * le sous-arbre /ietf-restconf-monitoring:restconf-state/capabilities.
 * Comme pour l'exemple oven, '*parent' vaut NULL au premier appel : il
 * faut donc reconstruire le chemin COMPLET depuis la racine du module a
 * chaque invocation (sysrepo ne conserve pas cet arbre entre deux
 * requetes).
 *
 * XXX-LY-API (meme prudence que src/sysrepo_backend.c) : lyd_new_path()
 * cree recursivement tout squelette d'ancetres manquant ; verifiez cette
 * signature dans votre libyang/tree_data.h si votre version differe. */
static int restconf_monitoring_capabilities_cb(sr_session_ctx_t *session, uint32_t sub_id,
                                                const char *module_name, const char *path,
                                                const char *request_xpath, uint32_t request_id,
                                                struct lyd_node **parent, void *private_data)
{
    const struct ly_ctx *ly_ctx;
    size_t i;

    (void)sub_id;
    (void)module_name;
    (void)path;
    (void)request_xpath;
    (void)request_id;
    (void)private_data;

    ly_ctx = sr_acquire_context(sr_session_get_connection(session));
    if (!ly_ctx) {
        return SR_ERR_INTERNAL;
    }

    for (i = 0; i < NCAPABILITY_URIS; i++) {
        LY_ERR lyrc;
        if (i == 0) {
            /* Premier appel : '*parent' est NULL, il faut donc passer le
             * contexte libyang explicitement (2e parametre) et laisser
             * lyd_new_path() construire tout le squelette d'ancetres
             * (restconf-state/capabilities) jusqu'a la premiere instance
             * de la leaf-list 'capability'. */
            lyrc = lyd_new_path(NULL, ly_ctx,
                                 "/ietf-restconf-monitoring:restconf-state/capabilities/capability",
                                 CAPABILITY_URIS[i], 0, parent);
        } else {
            /* Appels suivants : chaque nouvelle instance de la leaf-list
             * est accrochee au meme arbre ('*parent', deja construit),
             * sans repasser de contexte explicite (resolu depuis le
             * noeud parent lui-meme). */
            lyrc = lyd_new_path(*parent, NULL,
                                 "/ietf-restconf-monitoring:restconf-state/capabilities/capability",
                                 CAPABILITY_URIS[i], 0, NULL);
        }
        if (lyrc != LY_SUCCESS) {
            SRPLG_LOG_ERR("restconf-monitoring",
                          "echec de construction de restconf-state/capabilities/capability "
                          "(index %zu)",
                          i);
            sr_release_context(sr_session_get_connection(session));
            return SR_ERR_INTERNAL;
        }
    }

    sr_release_context(sr_session_get_connection(session));
    return SR_ERR_OK;
}

int sr_plugin_init_cb(sr_session_ctx_t *session, void **private_data)
{
    int rc;

    (void)private_data;

    /* RFC 8040 SS9.1 : restconf-state/capabilities est "config false" ; on
     * la fournit donc comme donnee operationnelle plutot que de la
     * pre-charger dans <running>/<startup> (ce que sysrepo ne permettrait
     * d'ailleurs pas pour un sous-arbre non-config). */
    rc = sr_oper_get_subscribe(session, "ietf-restconf-monitoring",
                               "/ietf-restconf-monitoring:restconf-state/capabilities",
                               restconf_monitoring_capabilities_cb, NULL, 0, &g_sub);
    if (rc != SR_ERR_OK) {
        SRPLG_LOG_ERR("restconf-monitoring",
                      "sr_oper_get_subscribe(restconf-state/capabilities) a echoue: %s",
                      sr_strerror(rc));
        return rc;
    }

    SRPLG_LOG_DBG("restconf-monitoring", "plugin restconf-monitoring initialise avec succes.");
    return SR_ERR_OK;
}

void sr_plugin_cleanup_cb(sr_session_ctx_t *session, void *private_data)
{
    (void)session;
    (void)private_data;

    sr_unsubscribe(g_sub);
    SRPLG_LOG_DBG("restconf-monitoring", "plugin restconf-monitoring nettoye.");
}
