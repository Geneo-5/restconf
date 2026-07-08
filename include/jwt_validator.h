#ifndef JWT_VALIDATOR_H
#define JWT_VALIDATOR_H

#include <stdint.h>
#include <stdbool.h>

typedef struct jwt_ctx_s jwt_ctx_t;

/**
 * Initialize the JWT validator.
 * Loads the public key from the Kernel Keyring (libkeyutils) and loads it into an in-memory OpenSSL context.
 * @param[in]  key_description Description of the key in the keyring (e.g., "restconf_jwt_pubkey").
 * @return     JWT context, or NULL if the key is not found/invalid.
 */
jwt_ctx_t *jwt_validator_init(const char *key_description);

/**
 * @brief Vérifie la signature d'un JWT et extrait le nom d'utilisateur.
 * @note Cette fonction est purement CPU (non-bloquante) car la clé est en RAM.
 * @param ctx Contexte JWT.
 * @param jwt_str Chaîne JWT complète (sans le préfixe "Bearer ").
 * @param username_out Buffer pour stocker le nom d'utilisateur (claim "sub").
 * @param username_len Taille du buffer.
 * @return 0 si valide, -1 si signature invalide ou expiré.
 */
int jwt_validator_verify(jwt_ctx_t *ctx, const char *jwt_str, 
						 char *username_out, size_t username_len);

/**
 * Destroy the JWT context and free all OpenSSL memory resources associated with it.
 */
void jwt_validator_destroy(jwt_ctx_t *ctx);

#endif // JWT_VALIDATOR_H
