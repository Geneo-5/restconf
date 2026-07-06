#ifndef UDS_COMMON_H
#define UDS_COMMON_H

#include <stdint.h>
#include <stddef.h>

// Types de messages IPC
typedef enum {
    IPC_MSG_AUTH_REQ,       // Gateway -> Plugin : Valider JWT
    IPC_MSG_AUTH_RES,       // Plugin -> Gateway : Résultat validation
    IPC_MSG_DATA_REQ,       // Gateway -> Plugin : GET sysrepo
    IPC_MSG_DATA_RES,       // Plugin -> Gateway : Données sysrepo
    IPC_MSG_EDIT_REQ,       // Gateway -> Plugin : POST/PUT/PATCH/DELETE
    IPC_MSG_EDIT_RES,       // Plugin -> Gateway : Statut édition
    IPC_MSG_RPC_REQ,        // Gateway -> Plugin : Invocation RPC
    IPC_MSG_RPC_RES,        // Plugin -> Gateway : Résultat RPC
    IPC_MSG_NOTIF_PUSH,     // Plugin -> Gateway : Notification YANG (SSE)
    IPC_MSG_PING            // Keep-alive UDS
} ipc_msg_type_t;

// En-tête de message IPC (Length-Header framing)
typedef struct __attribute__((packed)) {
    uint32_t magic;         // 0x52434E46 ("RCNF")
    uint32_t msg_id;        // ID unique pour corréler requête/réponse
    ipc_msg_type_t type;    // Type de message
    uint32_t payload_len;   // Taille du payload qui suit
    int32_t status_code;    // Code d'erreur sysrepo/HTTP (0 = OK)
} ipc_msg_header_t;

#define IPC_MAGIC_NUMBER 0x52434E46

/**
 * @brief Sérialise une requête dans un buffer pour l'envoi sur l'UDS.
 */
int ipc_serialize_request(ipc_msg_type_t type, uint32_t msg_id, 
                          const uint8_t *payload, size_t payload_len, 
                          uint8_t **out_buf, size_t *out_len);

/**
 * @brief Parse un en-tête IPC reçu depuis l'UDS.
 */
int ipc_parse_header(const uint8_t *buf, size_t len, ipc_msg_header_t *header_out);

#endif // UDS_COMMON_H