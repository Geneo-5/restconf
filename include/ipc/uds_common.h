#ifndef UDS_COMMON_H
#define UDS_COMMON_H

#include <stdint.h>
#include <stddef.h>

/* Types de messages IPC */
typedef enum {
	IPC_MSG_AUTH_REQ,	/* Gateway -> Plugin : Valider JWT */
	IPC_MSG_AUTH_RES,	/* Plugin -> Gateway : Resultat validation */
	IPC_MSG_DATA_REQ,	/* Gateway -> Plugin : GET sysrepo */
	IPC_MSG_DATA_RES,	/* Plugin -> Gateway : Donnees sysrepo */
	IPC_MSG_EDIT_REQ,	/* Gateway -> Plugin : POST/PUT/PATCH/DELETE */
	IPC_MSG_EDIT_RES,	/* Plugin -> Gateway : Statut edition */
	IPC_MSG_RPC_REQ,	/* Gateway -> Plugin : Invocation RPC */
	IPC_MSG_RPC_RES,	/* Plugin -> Gateway : Resultat RPC */
	IPC_MSG_NOTIF_PUSH,	/* Plugin -> Gateway : Notification YANG (SSE) */
	IPC_MSG_PING		/* Keep-alive UDS */
} ipc_msg_type_t;

/* En-tete de message IPC (Length-Header framing) */
typedef struct __attribute__((packed)) {
	uint32_t magic;		/* 0x52434E46 ("RCNF") */
	uint32_t msg_id;	/* ID unique pour correler requete/reponse */
	ipc_msg_type_t type;	/* Type de message */
	uint32_t payload_len;	/* Taille du payload qui suit */
	int32_t status_code;	/* Code d'erreur sysrepo/HTTP (0 = OK) */
} ipc_msg_header_t;

#define IPC_MAGIC_NUMBER 0x52434E46

/**
 * @brief Serialise un message (requete ou reponse) dans un buffer
 * pour l'envoi sur l'UDS. @p status_code est reporte tel quel dans
 * l'en-tete (code HTTP pour une reponse DATA_RES/RPC_RES/EDIT_RES,
 * 0 pour une requete sortante).
 */
int ipc_serialize_message(
	ipc_msg_type_t type, uint32_t msg_id, int32_t status_code,
	const uint8_t *payload, size_t payload_len,
	uint8_t **out_buf, size_t *out_len);

/**
 * @brief Alias historique de ipc_serialize_message() avec
 * status_code force a 0 (usage : requetes sortantes Gateway ->
 * Plugin, qui n'ont pas de statut avant reponse).
 */
int ipc_serialize_request(
	ipc_msg_type_t type, uint32_t msg_id,
	const uint8_t *payload, size_t payload_len,
	uint8_t **out_buf, size_t *out_len);

/**
 * @brief Parse un en-tete IPC recu depuis l'UDS.
 */
int ipc_parse_header(
	const uint8_t *buf, size_t len,
	ipc_msg_header_t *header_out);

#endif /* UDS_COMMON_H */
