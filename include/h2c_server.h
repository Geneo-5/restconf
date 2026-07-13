#ifndef H2C_SERVER_H
#define H2C_SERVER_H

#include <event2/event.h>
#include <nghttp2/nghttp2.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct h2c_server_s h2c_server_t;
typedef struct h2c_session_s h2c_session_t;

typedef void (*h2c_request_cb)(
	h2c_session_t *session, int32_t stream_id,
	const char *method, const char *path,
	const char *body, size_t body_len, void *user_data);

h2c_server_t *h2c_server_init(
	const char *bind_addr, uint16_t port,
	h2c_request_cb req_cb, void *user_data);

/**
 * @brief Initialise le serveur h2c sur une socket Unix (UDS).
 * @param uds_path Chemin vers la socket Unix (ex: /var/run/restconf.sock)
 * @param req_cb Callback pour traiter les requêtes HTTP/2
 * @param user_data Pointeur vers le contexte applicatif
 * @return Pointeur vers le serveur, ou NULL en cas d'erreur
 */
h2c_server_t *h2c_server_init_uds(
	const char *uds_path,
	h2c_request_cb req_cb, void *user_data);

void h2c_server_run(h2c_server_t *server);
void h2c_server_destroy(h2c_server_t *server);

/**
 * @brief Configure un timeout d'inactivite en lecture applique a
 * chaque connexion acceptee APRES cet appel (RFC 8040 Sec 12,
 * ROADMAP.md item 7.3). 0 (defaut) desactive le timeout.
 *
 * @note Timeout de LECTURE uniquement : ferme une connexion dont
 * le CLIENT n'envoie plus aucun octet pendant la duree indiquee.
 * Ne s'applique volontairement pas en ecriture, pour ne pas casser
 * les flux SSE longue duree ou seul le serveur emet (le ping
 * keep-alive de l'item 6.4 est une ecriture, elle ne reinitialise
 * donc pas ce timer) -- choisir une valeur assez genereuse pour ne
 * pas fermer un flux SSE legitime dont le client ne renvoie jamais
 * rien apres la requete initiale.
 */
void h2c_server_set_idle_timeout(
	h2c_server_t *server, int timeout_sec);

int h2c_send_response(
	h2c_session_t *session, int32_t stream_id,
	int status_code, const char *content_type,
	const char *location,
	const uint8_t *body, size_t body_len);

/**
 * @brief Envoie une réponse HTTP/2 avec un header additionnel.
 * @param extra_hdr_name  Nom du header additionnel (ex: "Allow")
 * @param extra_hdr_value Valeur du header (ex: "GET, HEAD, OPTIONS")
 */
int h2c_send_response_ex(
	h2c_session_t *session, int32_t stream_id,
	int status_code, const char *content_type,
	const char *location,
	const char *extra_hdr_name,
	const char *extra_hdr_value,
	const uint8_t *body, size_t body_len);

/**
 * @brief Envoie une réponse HTTP/2 avec des headers additionnels.
 * @param extra_headers Tableau de paires nom/valeur, NULL-terminé.
 *        Ex: {{"etag", "\"abc\""}, {"last-modified", "..."}, {NULL, NULL}}
 */
typedef struct {
	const char *name;
	const char *value;
} h2c_extra_header_t;

int h2c_send_response_with_headers(
	h2c_session_t *session, int32_t stream_id,
	int status_code, const char *content_type,
	const char *location,
	const h2c_extra_header_t *extra_headers,
	const uint8_t *body, size_t body_len);

/**
 * @brief Récupère la méthode HTTP de la requête en cours.
 */
const char *h2c_session_get_method(h2c_session_t *session);

/* Structure opaque pour un flux SSE */
typedef struct h2c_sse_stream_s h2c_sse_stream_t;

/**
 * @brief Démarre un flux SSE en envoyant les headers initiaux.
 * @return Pointeur vers le flux SSE, ou NULL en cas d'erreur.
 */
h2c_sse_stream_t *h2c_sse_stream_open(
	h2c_session_t *session, int32_t stream_id);

/**
 * @brief Pousse des données sur un flux SSE.
 * @note Non-bloquant, utilise la file d'attente de nghttp2.
 */
int h2c_sse_stream_push(
	h2c_sse_stream_t *stream,
	const uint8_t *data, size_t data_len);

/**
 * @brief Ferme un flux SSE (envoie END_STREAM).
 */
void h2c_sse_stream_close(h2c_sse_stream_t *stream);

struct event_base *h2c_server_get_event_base(h2c_server_t *server);

/**
 * @brief Récupère la session nghttp2 associée à une session h2c.
 * Utile pour les opérations avancées (resume data, etc.).
 */
nghttp2_session *h2c_session_get_nghttp2(h2c_session_t *session);

/* Récupère un header HTTP/2 stocké (ex: Authorization) */
const char *h2c_session_get_header(
	h2c_session_t *session, const char *name);

/**
 * @brief Récupère le header Content-Type de la requête.
 */
const char *h2c_session_get_content_type(
	h2c_session_t *session);

/**
 * @brief Récupère le header If-Match de la requête.
 */
const char *h2c_session_get_if_match(
	h2c_session_t *session);

/**
 * @brief Récupère le header Accept de la requête.
 */
const char *h2c_session_get_accept(
	h2c_session_t *session);

#endif // H2C_SERVER_H