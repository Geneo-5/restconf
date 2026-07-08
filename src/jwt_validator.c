#define _GNU_SOURCE
#define UNUSED __attribute__((unused))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/keyctl.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <json-c/json.h>
#include "jwt_validator.h"

struct jwt_ctx_s {
	EVP_PKEY *pkey;
};

/**
 * @brief Extrait une valeur string d'un objet JSON par son nom de clé.
 *
 * Utilise la bibliothèque json-c pour un parsing JSON robuste.
 *
 * @param json    Chaîne JSON (terminée par \0)
 * @param key     Nom de la clé recherchée
 * @param out     Buffer de sortie pour la valeur string
 * @param out_len Taille du buffer de sortie
 * @return 0 si trouvé, -1 sinon
 */
static int json_get_string_value(
	const char *json, const char *key,
	char *out, size_t out_len)
{
	struct json_object *root = NULL;
	struct json_object *value = NULL;
	const char *str_val = NULL;
	int ret = -1;

	if (!json || !key || !out || out_len == 0) return -1;

	/* Parser le JSON avec json-c */
	root = json_tokener_parse(json);
	if (!root) return -1;

	/* Chercher la clé et vérifier que c'est une string */
	if (json_object_object_get_ex(root, key, &value) &&
	    json_object_is_type(value, json_type_string)) {
		str_val = json_object_get_string(value);
		if (str_val) {
			strncpy(out, str_val, out_len - 1);
			out[out_len - 1] = '\0';
			ret = 0;
		}
	}

	/* Libérer l'objet JSON */
	json_object_put(root);
	return ret;
}

/**
 * @brief Décode une chaîne Base64URL (spécifique aux JWT) en binaire.
 * Utilise OpenSSL (EVP_DecodeBlock) déjà lié au projet.
 */
static int base64url_decode(
	const char *in, size_t in_len,
	uint8_t *out, size_t *out_len)
{
	size_t i, pad_len;
	char *std_b64;
	int decoded_len;

	if (!in || !out || !out_len) return -1;

	/* 1. Allouer un buffer pour le Base64 standard (avec padding) */
	/* Le padding max est de 3 caractères '=' */
	std_b64 = malloc(in_len + 4);
	if (!std_b64) return -1;

	/* 2. Convertir Base64URL -> Base64 standard */
	for (i = 0; i < in_len; i++) {
		if (in[i] == '-') {
			std_b64[i] = '+';
		} else if (in[i] == '_') {
			std_b64[i] = '/';
		} else {
			std_b64[i] = in[i];
		}
	}

	/* 3. Rajouter le padding '=' manquant (requis par EVP_DecodeBlock) */
	pad_len = (4 - (in_len % 4)) % 4;
	for (i = 0; i < pad_len; i++) {
		std_b64[in_len + i] = '=';
	}
	std_b64[in_len + pad_len] = '\0';

	/* 4. Décoder via OpenSSL */
	/* EVP_DecodeBlock ne gère pas les espaces, mais notre JWT n'en a pas */
	decoded_len = EVP_DecodeBlock(
		out, (const unsigned char *)std_b64,
		in_len + pad_len);

	free(std_b64);

	if (decoded_len < 0) {
		return -1; /* Erreur de décodage */
	}

	/* 5. Ajuster la taille de sortie (EVP_DecodeBlock inclut le padding) */
	if (pad_len > 0 && decoded_len >= (int)pad_len) {
		decoded_len -= pad_len;
	}

	*out_len = (size_t)decoded_len;
	return 0;
}

jwt_ctx_t *jwt_validator_init(const char *key_description)
{
	jwt_ctx_t *ctx = calloc(1, sizeof(jwt_ctx_t));
	if (!ctx) return NULL;

#ifdef ALLOW_INSECURE_JWT
	fprintf(stderr, 
		"🚨 WARNING: JWT signature verification DISABLED!\n");
	return ctx; /* Retourne un contexte vide, pas de clé nécessaire */
#else
	/* 1. Chercher la clé dans le Kernel Keyring via syscall */
	long key_id = syscall(
		__NR_request_key, "user",
		key_description, NULL,
		KEY_SPEC_SESSION_KEYRING);

	if (key_id < 0) {
		perror("request_key syscall failed");
		free(ctx);
		return NULL;
	}

	long key_len = syscall(
		__NR_keyctl, KEYCTL_READ, key_id, NULL, 0);
	if (key_len < 0) { free(ctx); return NULL; }

	char *key_buf = malloc(key_len + 1);
	if (!key_buf) { free(ctx); return NULL; }

	long read_len = syscall(
		__NR_keyctl, KEYCTL_READ, key_id, key_buf, key_len);
	if (read_len < 0) { free(key_buf); free(ctx); return NULL; }
	key_buf[read_len] = '\0';

	BIO *bio = BIO_new_mem_buf(key_buf, read_len);
	if (!bio) { free(key_buf); free(ctx); return NULL; }

	ctx->pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
	BIO_free(bio);
	free(key_buf);

	if (!ctx->pkey) {
		fprintf(stderr, "Failed to parse public key\n");
		free(ctx);
		return NULL;
	}

	return ctx;
#endif
}

int jwt_validator_verify(
	jwt_ctx_t *ctx, const char *jwt_str,
	char *username_out, size_t username_len)
{
	if (!ctx || !jwt_str) return -1;

	char *jwt_copy = strdup(jwt_str);
	char *header_b64 = jwt_copy;
	char *payload_b64 = strchr(jwt_copy, '.');
	if (!payload_b64) { free(jwt_copy); return -1; }
	*payload_b64++ = '\0';
	
	char *signature_b64 = strchr(payload_b64, '.');
	if (!signature_b64) { free(jwt_copy); return -1; }
	*signature_b64++ = '\0';

#ifndef ALLOW_INSECURE_JWT
	/* --- MODE PRODUCTION : Vérification cryptographique --- */
	if (!ctx->pkey) {
		free(jwt_copy);
		return -1;
	}

	EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
	if (!mdctx) { free(jwt_copy); return -1; }

	if (EVP_DigestVerifyInit(
	        mdctx, NULL, EVP_sha256(), NULL,
	        ctx->pkey) != 1) {
		EVP_MD_CTX_free(mdctx);
		free(jwt_copy);
		return -1;
	}

	EVP_DigestVerifyUpdate(mdctx, header_b64, strlen(header_b64));
	EVP_DigestVerifyUpdate(mdctx, ".", 1);
	EVP_DigestVerifyUpdate(mdctx, payload_b64, strlen(payload_b64));

	uint8_t sig[256];
	size_t sig_len = sizeof(sig);
	base64url_decode(
		signature_b64, strlen(signature_b64),
		sig, &sig_len);

	int verify_result = EVP_DigestVerifyFinal(mdctx, sig, sig_len);
	EVP_MD_CTX_free(mdctx);

	if (verify_result != 1) {
		free(jwt_copy);
		return -1; /* Signature invalide */
	}
#else
	/* --- MODE DEBUG : Ignorer la signature --- */
	(void)signature_b64;
	(void)header_b64;
#endif

	/* --- EXTRACTION DU PAYLOAD (Commun aux deux modes) --- */
	uint8_t payload_json[4096];
	size_t json_len = sizeof(payload_json);
	base64url_decode(
		payload_b64, strlen(payload_b64),
		payload_json, &json_len);
	payload_json[json_len] = '\0';

	/* Extraction robuste du claim "sub" (RFC 7519 §4.1.2) */
	json_get_string_value(
		(const char *)payload_json, "sub",
		username_out, username_len);

	free(jwt_copy);
	return 0;
}

void jwt_validator_destroy(jwt_ctx_t *ctx)
{
	if (ctx) {
#ifndef ALLOW_INSECURE_JWT
		if (ctx->pkey) EVP_PKEY_free(ctx->pkey);
#endif
		free(ctx);
	}
}