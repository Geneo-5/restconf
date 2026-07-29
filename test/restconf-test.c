/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

/**
 * @file
 * @brief Plugin sysrepo-plugind de qualification RESTCONF (module
 *        @c restconf-test).
 *
 * Ce plugin fournit la partie « vivante » du module YANG de qualification
 * @c restconf-test (namespace @c urn:restconf:test, préfixe @c rt), déclaré
 * dans @c test/restconf-test.yang. Il est compilé en @c restconf-test.so puis
 * installé dans le dossier des plugins de sysrepo-plugind
 * (@c CONFIG_SYSREPO_PLUGIND_PATH, voir @c test/ebuild.mk) ; sysrepo-plugind
 * le charge automatiquement au démarrage et appelle ses points d'entrée
 * @c sr_plugin_init_cb() / @c sr_plugin_cleanup_cb().
 *
 * Il prend en charge tout ce que le datastore statique
 * (@c test/restconf-test.json) ne peut pas fournir :
 *
 *  - les données opérationnelles (@c config false) :
 *      - /restconf-test:system/state/system-status   (string)
 *      - /restconf-test:system/state/extended-status (string, si la feature
 *        'advanced-monitoring' est activée à l'installation) ;
 *      - /restconf-test:basic-data/uptime            (uint32, secondes) ;
 *
 *  - les RPC de premier niveau (RFC 8040 §4.4.2, RFC 7950 §7.15) :
 *      - get-system-status, configure-device, create-resource,
 *        set-operation-mode, process-data, trigger-event ;
 *
 *  - les actions YANG 1.1 (RFC 7950 §7.15) :
 *      - device-management/reset, device-management/test-connection,
 *        device-management/managed-device/reboot ;
 *
 *  - l'émission de notifications (RFC 8040 §6, RFC 8639) : le RPC
 *    'trigger-event' émet une notification 'event-notification' (best-effort).
 *
 * Les valeurs retournées sont déterministes et sans effet de bord réel : le
 * but est de qualifier le serveur RESTCONF (encodage JSON/XML, routage des
 * RPC/actions, enveloppes d'erreur, flux de notifications), pas de piloter un
 * équipement. L'état simulé (compteurs, mode d'opération, heure de départ)
 * est conservé dans quelques variables globales, à l'image de l'exemple
 * 'oven' fourni avec sysrepo.
 *
 * Robustesse : si le module 'restconf-test' n'est pas installé dans sysrepo
 * au moment du chargement, le plugin se met en mode inactif (aucune
 * souscription) et n'échoue pas, afin de ne pas bloquer sysrepo-plugind.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* gmtime_r() */
#endif

#include <sysrepo.h>
#include <libyang/libyang.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

/** Nom du module YANG servi par ce plugin. */
#define RT_MODULE	"restconf-test"
/** Étiquette utilisée pour la journalisation sysrepo (SRPLG_LOG_*). */
#define RT_LOG		"restconf-test"
/** Taille des tampons de formatage des horodatages RFC 3339. */
#define RT_TS_LEN	64
/** Taille des tampons de formatage des entiers. */
#define RT_NUM_LEN	32
/** Taille du tampon conservant le mode d'opération courant (enum). */
#define RT_MODE_LEN	16

/**
 * @def RT_PUBLIC
 * @brief Force la visibilité par défaut d'un symbole.
 *
 * Le build du projet compile avec @c -fvisibility=hidden (voir
 * @c common.mk) ; les points d'entrée attendus par sysrepo-plugind doivent
 * donc être explicitement exportés, sans quoi ils seraient invisibles du
 * chargeur de plugins.
 */
#define RT_PUBLIC	__attribute__((visibility("default")))

/* ---------------------------------------------------------------------------
 * État simulé du plugin (inspiré de l'exemple 'oven' de sysrepo).
 *
 * Aucune synchronisation n'est mise en place : comme pour l'exemple 'oven',
 * le risque de course est jugé négligeable pour un plugin de qualification.
 * ------------------------------------------------------------------------ */

/** Session du plugin, valable jusqu'à l'appel de sr_plugin_cleanup_cb(). */
static sr_session_ctx_t *rt_sess;
/** Regroupe l'ensemble des souscriptions créées à l'initialisation. */
static sr_subscription_ctx_t *rt_subscription;
/** Instant de départ simulé, pour le calcul de 'uptime' (secondes). */
static time_t rt_start_time;
/** Identifiant attribué au prochain 'configure-device' (uint32). */
static uint32_t rt_next_device_id;
/** Identifiant attribué au prochain 'create-resource' (uint32). */
static uint32_t rt_next_resource_id;
/** Mode d'opération courant, renvoyé par 'set-operation-mode'. */
static char rt_current_mode[RT_MODE_LEN];

/* ---------------------------------------------------------------------------
 * Déclaration des points d'entrée du plugin.
 *
 * sysrepo-plugind résout ces deux symboles dans chaque .so chargé. La
 * déclaration préalable satisfait l'option -Wmissing-declarations (active en
 * -Werror dans le build CI) et porte l'attribut de visibilité.
 * ------------------------------------------------------------------------ */

RT_PUBLIC int sr_plugin_init_cb(sr_session_ctx_t *session, void **private_data);
RT_PUBLIC void sr_plugin_cleanup_cb(sr_session_ctx_t *session, void *private_data);

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

/**
 * @brief Formate l'instant courant en horodatage RFC 3339 (yang:date-and-time).
 *
 * @param[out] buf tampon de réception (au moins @p len octets).
 * @param len      taille de @p buf.
 */
static void
rt_iso8601(char *buf, size_t len)
{
	struct tm tm_utc;
	time_t now;

	now = time(NULL);
	(void)gmtime_r(&now, &tm_utc);
	if (strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &tm_utc) == 0) {
		/* Tampon trop petit (ne devrait pas arriver) : chaîne sûre. */
		snprintf(buf, len, "1970-01-01T00:00:00Z");
	}
}

/**
 * @brief Lit la valeur canonique d'une feuille d'entrée d'un RPC/action.
 *
 * Parcourt les enfants directs du nœud d'opération @p input (les feuilles
 * définies dans le bloc 'input' du modèle) et retourne la valeur canonique de
 * celle nommée @p name.
 *
 * @param input nœud d'opération fourni par sysrepo (peut être @c NULL).
 * @param name  nom de la feuille recherchée.
 * @return valeur canonique (chaîne), ou @c NULL si absente.
 */
static const char *
rt_input_value(const struct lyd_node *input, const char *name)
{
	const struct lyd_node *child;

	if (input == NULL) {
		return NULL;
	}

	LY_LIST_FOR(lyd_child(input), child) {
		if (strcmp(LYD_NAME(child), name) == 0) {
			return lyd_get_value(child);
		}
	}

	return NULL;
}

/**
 * @brief Crée une feuille de sortie d'un RPC/action.
 *
 * @p output est le nœud d'opération pré-créé par sysrepo ; la feuille est
 * ajoutée comme enfant direct via un chemin relatif, avec l'option
 * @c LYD_NEW_PATH_OUTPUT afin que libyang la recherche parmi les nœuds de
 * sortie du modèle. Le chemin relatif fonctionne aussi bien pour un RPC de
 * premier niveau que pour une action portée par une instance de liste.
 *
 * @param output nœud d'opération (racine de sortie).
 * @param name   nom de la feuille de sortie.
 * @param value  valeur (chaîne canonique).
 * @return @c SR_ERR_OK en cas de succès, @c SR_ERR_OPERATION_FAILED sinon.
 */
static int
rt_add_output(struct lyd_node *output, const char *name, const char *value)
{
	if (lyd_new_path(output, NULL, name, value, LYD_NEW_PATH_OUTPUT, NULL)
			!= LY_SUCCESS) {
		SRPLG_LOG_ERR(RT_LOG, "lyd_new_path(output '%s') failed.", name);
		return SR_ERR_OPERATION_FAILED;
	}

	return SR_ERR_OK;
}

/**
 * @brief Crée une feuille de sortie entière (uint32) d'un RPC/action.
 *
 * @param output nœud d'opération (racine de sortie).
 * @param name   nom de la feuille de sortie.
 * @param value  valeur entière.
 * @return @c SR_ERR_OK en cas de succès, @c SR_ERR_OPERATION_FAILED sinon.
 */
static int
rt_add_output_u32(struct lyd_node *output, const char *name, uint32_t value)
{
	char num[RT_NUM_LEN];
	int n;

	n = snprintf(num, sizeof(num), "%u", (unsigned)value);
	if ((n < 0) || ((size_t)n >= sizeof(num))) {
		return SR_ERR_OPERATION_FAILED;
	}

	return rt_add_output(output, name, num);
}

/* ---------------------------------------------------------------------------
 * Données opérationnelles (config false)
 * ------------------------------------------------------------------------ */

/**
 * @brief Fournit /restconf-test:system/state.
 *
 * - 'system-status' est toujours fourni ;
 * - 'extended-status' n'existe dans le schéma que si la feature
 *   'advanced-monitoring' est activée : sa création est donc tentée puis
 *   ignorée si le nœud est absent (LY_ENOTFOUND).
 */
static int
rt_system_state_cb(sr_session_ctx_t *session, uint32_t sub_id,
	const char *module_name, const char *path, const char *request_xpath,
	uint32_t request_id, struct lyd_node **parent, void *private_data)
{
	const struct ly_ctx *ly_ctx;
	LY_ERR rc;

	(void)sub_id;
	(void)module_name;
	(void)path;
	(void)request_xpath;
	(void)request_id;
	(void)private_data;

	ly_ctx = sr_acquire_context(sr_session_get_connection(session));
	if (ly_ctx == NULL) {
		SRPLG_LOG_ERR(RT_LOG, "Unable to acquire libyang context.");
		return SR_ERR_OPERATION_FAILED;
	}

	/* Crée system/state/system-status ; *parent pointe alors sur 'system'. */
	rc = lyd_new_path(NULL, ly_ctx,
		"/restconf-test:system/state/system-status", "operational", 0, parent);
	if (rc != LY_SUCCESS) {
		SRPLG_LOG_ERR(RT_LOG, "system-status creation failed (%d).", (int)rc);
		sr_release_context(sr_session_get_connection(session));
		return SR_ERR_OPERATION_FAILED;
	}

	/* extended-status : présent uniquement si la feature est activée. */
	rc = lyd_new_path(*parent, NULL, "state/extended-status", "nominal", 0, NULL);
	if ((rc != LY_SUCCESS) && (rc != LY_ENOTFOUND)) {
		SRPLG_LOG_WRN(RT_LOG, "extended-status creation failed (%d).", (int)rc);
	}

	sr_release_context(sr_session_get_connection(session));
	return SR_ERR_OK;
}

/**
 * @brief Fournit /restconf-test:basic-data/uptime (secondes depuis le départ).
 */
static int
rt_uptime_cb(sr_session_ctx_t *session, uint32_t sub_id,
	const char *module_name, const char *path, const char *request_xpath,
	uint32_t request_id, struct lyd_node **parent, void *private_data)
{
	const struct ly_ctx *ly_ctx;
	char num[RT_NUM_LEN];
	uint32_t uptime;
	time_t now;
	int n;

	(void)sub_id;
	(void)module_name;
	(void)path;
	(void)request_xpath;
	(void)request_id;
	(void)private_data;

	now = time(NULL);
	uptime = (uint32_t)(now - rt_start_time);

	n = snprintf(num, sizeof(num), "%u", (unsigned)uptime);
	if ((n < 0) || ((size_t)n >= sizeof(num))) {
		return SR_ERR_OPERATION_FAILED;
	}

	ly_ctx = sr_acquire_context(sr_session_get_connection(session));
	if (ly_ctx == NULL) {
		SRPLG_LOG_ERR(RT_LOG, "Unable to acquire libyang context.");
		return SR_ERR_OPERATION_FAILED;
	}

	/* *parent reste NULL : chemin absolu complet, créé d'un bloc. */
	if (lyd_new_path(NULL, ly_ctx, "/restconf-test:basic-data/uptime", num, 0,
			parent) != LY_SUCCESS) {
		SRPLG_LOG_ERR(RT_LOG, "uptime creation failed.");
		sr_release_context(sr_session_get_connection(session));
		return SR_ERR_OPERATION_FAILED;
	}

	sr_release_context(sr_session_get_connection(session));
	return SR_ERR_OK;
}

/* ---------------------------------------------------------------------------
 * RPC de premier niveau (RFC 8040 §4.4.2)
 * ------------------------------------------------------------------------ */

/**
 * @brief get-system-status : sans entrée ; sortie status + timestamp.
 */
static int
rt_get_system_status_cb(sr_session_ctx_t *session, uint32_t sub_id,
	const char *op_path, const struct lyd_node *input, sr_event_t event,
	uint32_t request_id, struct lyd_node *output, void *private_data)
{
	char ts[RT_TS_LEN];
	int rc;

	(void)session;
	(void)sub_id;
	(void)op_path;
	(void)input;
	(void)event;
	(void)request_id;
	(void)private_data;

	rt_iso8601(ts, sizeof(ts));

	rc = rt_add_output(output, "status", "operational");
	if (rc != SR_ERR_OK) {
		return rc;
	}
	return rt_add_output(output, "timestamp", ts);
}

/**
 * @brief configure-device : entrée device-name (mandatory), enable, settings ;
 *        sortie result + device-id.
 */
static int
rt_configure_device_cb(sr_session_ctx_t *session, uint32_t sub_id,
	const char *op_path, const struct lyd_node *input, sr_event_t event,
	uint32_t request_id, struct lyd_node *output, void *private_data)
{
	const char *device_name;
	int rc;

	(void)session;
	(void)sub_id;
	(void)op_path;
	(void)event;
	(void)request_id;
	(void)private_data;

	device_name = rt_input_value(input, "device-name");
	if (device_name == NULL) {
		SRPLG_LOG_ERR(RT_LOG, "configure-device: missing 'device-name'.");
		return SR_ERR_INVAL_ARG;
	}

	rc = rt_add_output(output, "result", "configured");
	if (rc != SR_ERR_OK) {
		return rc;
	}
	rc = rt_add_output_u32(output, "device-id", rt_next_device_id);
	if (rc != SR_ERR_OK) {
		return rc;
	}
	rt_next_device_id++;

	return SR_ERR_OK;
}

/**
 * @brief create-resource : entrée name (mandatory) ; sortie id.
 */
static int
rt_create_resource_cb(sr_session_ctx_t *session, uint32_t sub_id,
	const char *op_path, const struct lyd_node *input, sr_event_t event,
	uint32_t request_id, struct lyd_node *output, void *private_data)
{
	int rc;

	(void)session;
	(void)sub_id;
	(void)op_path;
	(void)event;
	(void)request_id;
	(void)private_data;

	if (rt_input_value(input, "name") == NULL) {
		SRPLG_LOG_ERR(RT_LOG, "create-resource: missing 'name'.");
		return SR_ERR_INVAL_ARG;
	}

	rc = rt_add_output_u32(output, "id", rt_next_resource_id);
	if (rc != SR_ERR_OK) {
		return rc;
	}
	rt_next_resource_id++;

	return SR_ERR_OK;
}

/**
 * @brief set-operation-mode : entrée mode (enum, mandatory) ; sortie
 *        previous-mode (mode antérieur conservé).
 */
static int
rt_set_operation_mode_cb(sr_session_ctx_t *session, uint32_t sub_id,
	const char *op_path, const struct lyd_node *input, sr_event_t event,
	uint32_t request_id, struct lyd_node *output, void *private_data)
{
	const char *mode;
	int rc;

	(void)session;
	(void)sub_id;
	(void)op_path;
	(void)event;
	(void)request_id;
	(void)private_data;

	mode = rt_input_value(input, "mode");
	if (mode == NULL) {
		SRPLG_LOG_ERR(RT_LOG, "set-operation-mode: missing 'mode'.");
		return SR_ERR_INVAL_ARG;
	}

	/* Renvoie le mode antérieur, puis mémorise le nouveau. */
	rc = rt_add_output(output, "previous-mode", rt_current_mode);
	if (rc != SR_ERR_OK) {
		return rc;
	}
	snprintf(rt_current_mode, sizeof(rt_current_mode), "%s", mode);

	return SR_ERR_OK;
}

/**
 * @brief process-data : entrée uint-value, enum-value ; sortie processed.
 */
static int
rt_process_data_cb(sr_session_ctx_t *session, uint32_t sub_id,
	const char *op_path, const struct lyd_node *input, sr_event_t event,
	uint32_t request_id, struct lyd_node *output, void *private_data)
{
	(void)session;
	(void)sub_id;
	(void)op_path;
	(void)input;
	(void)event;
	(void)request_id;
	(void)private_data;

	return rt_add_output(output, "processed", "true");
}

/**
 * @brief trigger-event : entrée event-type (mandatory) ; sans sortie.
 *
 * Émet une notification 'event-notification' (best-effort : l'échec d'émission
 * ne fait pas échouer le RPC, conformément à une sémantique « déclencher et
 * oublier »).
 */
static int
rt_trigger_event_cb(sr_session_ctx_t *session, uint32_t sub_id,
	const char *op_path, const struct lyd_node *input, sr_event_t event,
	uint32_t request_id, struct lyd_node *output, void *private_data)
{
	const struct ly_ctx *ly_ctx;
	const char *event_type;
	struct lyd_node *notif = NULL;
	char ts[RT_TS_LEN];
	int rc = SR_ERR_OK;

	(void)sub_id;
	(void)op_path;
	(void)event;
	(void)request_id;
	(void)output;
	(void)private_data;

	event_type = rt_input_value(input, "event-type");
	if (event_type == NULL) {
		SRPLG_LOG_ERR(RT_LOG, "trigger-event: missing 'event-type'.");
		return SR_ERR_INVAL_ARG;
	}

	ly_ctx = sr_acquire_context(sr_session_get_connection(session));
	if (ly_ctx == NULL) {
		SRPLG_LOG_WRN(RT_LOG, "trigger-event: no context, notification skipped.");
		return SR_ERR_OK;
	}

	rt_iso8601(ts, sizeof(ts));

	if (lyd_new_path(NULL, ly_ctx, "/restconf-test:event-notification", NULL, 0,
			&notif) != LY_SUCCESS) {
		SRPLG_LOG_WRN(RT_LOG, "trigger-event: notification creation failed.");
		goto out;
	}
	(void)lyd_new_path(notif, NULL, "event-type", event_type, 0, NULL);
	(void)lyd_new_path(notif, NULL, "severity", "info", 0, NULL);
	(void)lyd_new_path(notif, NULL, "source", RT_LOG, 0, NULL);
	(void)lyd_new_path(notif, NULL, "description",
		"Event triggered via trigger-event RPC", 0, NULL);
	(void)lyd_new_path(notif, NULL, "timestamp", ts, 0, NULL);

	rc = sr_notif_send_tree(session, notif, 0, 0);
	if (rc != SR_ERR_OK) {
		/* Pas d'abonné, ou émission refusée : non fatal pour le RPC. */
		SRPLG_LOG_WRN(RT_LOG, "trigger-event: notification not sent: %s.",
			sr_strerror(rc));
		rc = SR_ERR_OK;
	}

out:
	lyd_free_all(notif);
	sr_release_context(sr_session_get_connection(session));
	return rc;
}

/* ---------------------------------------------------------------------------
 * Actions YANG 1.1 (RFC 7950 §7.15)
 * ------------------------------------------------------------------------ */

/**
 * @brief device-management/reset : sans entrée ; sortie status.
 */
static int
rt_reset_cb(sr_session_ctx_t *session, uint32_t sub_id, const char *op_path,
	const struct lyd_node *input, sr_event_t event, uint32_t request_id,
	struct lyd_node *output, void *private_data)
{
	(void)session;
	(void)sub_id;
	(void)op_path;
	(void)input;
	(void)event;
	(void)request_id;
	(void)private_data;

	return rt_add_output(output, "status", "reset-complete");
}

/**
 * @brief device-management/test-connection : entrée target (mandatory),
 *        timeout ; sortie success + latency.
 */
static int
rt_test_connection_cb(sr_session_ctx_t *session, uint32_t sub_id,
	const char *op_path, const struct lyd_node *input, sr_event_t event,
	uint32_t request_id, struct lyd_node *output, void *private_data)
{
	int rc;

	(void)session;
	(void)sub_id;
	(void)op_path;
	(void)event;
	(void)request_id;
	(void)private_data;

	if (rt_input_value(input, "target") == NULL) {
		SRPLG_LOG_ERR(RT_LOG, "test-connection: missing 'target'.");
		return SR_ERR_INVAL_ARG;
	}

	rc = rt_add_output(output, "success", "true");
	if (rc != SR_ERR_OK) {
		return rc;
	}
	return rt_add_output_u32(output, "latency", 1);
}

/**
 * @brief device-management/managed-device/reboot : entrée force ; sortie
 *        status + reboot-time.
 *
 * Action portée par une instance de la liste 'managed-device' : le nœud
 * 'output' reçu est déjà positionné sous la bonne instance, les feuilles de
 * sortie sont donc ajoutées par chemin relatif.
 */
static int
rt_reboot_cb(sr_session_ctx_t *session, uint32_t sub_id, const char *op_path,
	const struct lyd_node *input, sr_event_t event, uint32_t request_id,
	struct lyd_node *output, void *private_data)
{
	int rc;

	(void)session;
	(void)sub_id;
	(void)op_path;
	(void)input;
	(void)event;
	(void)request_id;
	(void)private_data;

	rc = rt_add_output(output, "status", "rebooted");
	if (rc != SR_ERR_OK) {
		return rc;
	}
	return rt_add_output_u32(output, "reboot-time", 30);
}

/* ---------------------------------------------------------------------------
 * Points d'entrée du plugin
 * ------------------------------------------------------------------------ */

int
sr_plugin_init_cb(sr_session_ctx_t *session, void **private_data)
{
	const struct ly_ctx *ly_ctx;
	int rc = SR_ERR_OK;

	(void)private_data;

	rt_sess = session;
	rt_start_time = time(NULL);
	rt_next_device_id = 1;
	rt_next_resource_id = 1;
	snprintf(rt_current_mode, sizeof(rt_current_mode), "normal");

	/* Si le module n'est pas installé, le plugin reste inactif (no-op) afin
	 * de ne pas faire échouer le chargement par sysrepo-plugind. */
	ly_ctx = sr_acquire_context(sr_session_get_connection(session));
	if ((ly_ctx == NULL)
			|| (ly_ctx_get_module(ly_ctx, RT_MODULE, NULL) == NULL)) {
		if (ly_ctx != NULL) {
			sr_release_context(sr_session_get_connection(session));
		}
		SRPLG_LOG_WRN(RT_LOG,
			"Module '" RT_MODULE "' not installed, plugin disabled.");
		return SR_ERR_OK;
	}
	sr_release_context(sr_session_get_connection(session));

	/* --- Données opérationnelles (config false) --- */
	rc = sr_oper_get_subscribe(session, RT_MODULE,
		"/restconf-test:system/state", rt_system_state_cb, NULL, 0,
		&rt_subscription);
	if (rc != SR_ERR_OK) {
		goto error;
	}
	rc = sr_oper_get_subscribe(session, RT_MODULE,
		"/restconf-test:basic-data/uptime", rt_uptime_cb, NULL, 0,
		&rt_subscription);
	if (rc != SR_ERR_OK) {
		goto error;
	}

	/* --- RPC de premier niveau --- */
	rc = sr_rpc_subscribe_tree(session, "/restconf-test:get-system-status",
		rt_get_system_status_cb, NULL, 0, 0, &rt_subscription);
	if (rc != SR_ERR_OK) {
		goto error;
	}
	rc = sr_rpc_subscribe_tree(session, "/restconf-test:configure-device",
		rt_configure_device_cb, NULL, 0, 0, &rt_subscription);
	if (rc != SR_ERR_OK) {
		goto error;
	}
	rc = sr_rpc_subscribe_tree(session, "/restconf-test:create-resource",
		rt_create_resource_cb, NULL, 0, 0, &rt_subscription);
	if (rc != SR_ERR_OK) {
		goto error;
	}
	rc = sr_rpc_subscribe_tree(session, "/restconf-test:set-operation-mode",
		rt_set_operation_mode_cb, NULL, 0, 0, &rt_subscription);
	if (rc != SR_ERR_OK) {
		goto error;
	}
	rc = sr_rpc_subscribe_tree(session, "/restconf-test:process-data",
		rt_process_data_cb, NULL, 0, 0, &rt_subscription);
	if (rc != SR_ERR_OK) {
		goto error;
	}
	rc = sr_rpc_subscribe_tree(session, "/restconf-test:trigger-event",
		rt_trigger_event_cb, NULL, 0, 0, &rt_subscription);
	if (rc != SR_ERR_OK) {
		goto error;
	}

	/* --- Actions YANG 1.1 --- */
	rc = sr_rpc_subscribe_tree(session, "/restconf-test:device-management/reset",
		rt_reset_cb, NULL, 0, 0, &rt_subscription);
	if (rc != SR_ERR_OK) {
		goto error;
	}
	rc = sr_rpc_subscribe_tree(session,
		"/restconf-test:device-management/test-connection",
		rt_test_connection_cb, NULL, 0, 0, &rt_subscription);
	if (rc != SR_ERR_OK) {
		goto error;
	}
	rc = sr_rpc_subscribe_tree(session,
		"/restconf-test:device-management/managed-device/reboot",
		rt_reboot_cb, NULL, 0, 0, &rt_subscription);
	if (rc != SR_ERR_OK) {
		goto error;
	}

	SRPLG_LOG_DBG(RT_LOG, "Plugin initialized successfully.");
	return SR_ERR_OK;

error:
	SRPLG_LOG_ERR(RT_LOG, "Plugin initialization failed: %s.", sr_strerror(rc));
	sr_unsubscribe(rt_subscription);
	rt_subscription = NULL;
	return rc;
}

void
sr_plugin_cleanup_cb(sr_session_ctx_t *session, void *private_data)
{
	(void)session;
	(void)private_data;

	sr_unsubscribe(rt_subscription);
	rt_subscription = NULL;
	SRPLG_LOG_DBG(RT_LOG, "Plugin cleanup finished.");
}
