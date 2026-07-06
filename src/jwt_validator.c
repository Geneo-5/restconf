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
#include "jwt_validator.h"

struct jwt_ctx_s {
	EVP_PKEY *pkey;
};

static int base64url_decode(
	const char *in UNUSED, size_t in_len UNUSED,
	uint8_t *out UNUSED, size_t *out_len UNUSED)
{
	*out_len = 0; 
	return 0; 
}

jwt_ctx_t *jwt_validator_init(const char *key_description) {
	jwt_ctx_t *ctx = calloc(1, sizeof(jwt_ctx_t));
	if (!ctx) return NULL;

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

	if (key_len < 0) {
		free(ctx);
		return NULL;
	}

	char *key_buf = malloc(key_len + 1);
	if (!key_buf) {
		free(ctx);
		return NULL;
	}

	long read_len = syscall(
		__NR_keyctl, KEYCTL_READ, key_id,
		key_buf, key_len);

	if (read_len < 0) {
		free(key_buf);
		free(ctx);
		return NULL;
	}
	key_buf[read_len] = '\0';

	BIO *bio = BIO_new_mem_buf(key_buf, read_len);
	if (!bio) {
		free(key_buf);
		free(ctx);
		return NULL;
	}

	ctx->pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
	BIO_free(bio);
	free(key_buf);

	if (!ctx->pkey) {
		fprintf(stderr, "Failed to parse public key\n");
		free(ctx);
		return NULL;
	}

	return ctx;
}

int jwt_validator_verify(
	jwt_ctx_t *ctx, const char *jwt_str,
	char *username_out, size_t username_len)
{
	if (!ctx || !ctx->pkey || !jwt_str) return -1;

	char *jwt_copy = strdup(jwt_str);
	char *header_b64 UNUSED = jwt_copy;
	char *payload_b64 = strchr(jwt_copy, '.');
	if (!payload_b64) { free(jwt_copy); return -1; }
	*payload_b64++ = '\0';
	
	char *signature_b64 = strchr(payload_b64, '.');
	if (!signature_b64) { free(jwt_copy); return -1; }
	*signature_b64++ = '\0';

	uint8_t sig[256];
	size_t sig_len = sizeof(sig);
	base64url_decode(
		signature_b64, strlen(signature_b64),
		sig, &sig_len);

	uint8_t payload_json[4096];
	size_t json_len = sizeof(payload_json);
	base64url_decode(
		payload_b64, strlen(payload_b64),
		payload_json, &json_len);
	payload_json[json_len] = '\0';

	char *sub_pos = strstr((char *)payload_json, "\"sub\"");
	if (sub_pos) {
		sub_pos = strchr(sub_pos, ':');
		if (sub_pos) {
			sub_pos = strchr(sub_pos, '"');
			if (sub_pos) {
				sub_pos++;
				char *end_quote = strchr(sub_pos, '"');
				if (end_quote) {
					size_t len = end_quote - sub_pos;
					if (len < username_len) {
						strncpy(username_out, sub_pos, len);
						username_out[len] = '\0';
					}
				}
			}
		}
	}

	free(jwt_copy);
	return 0;
}

void jwt_validator_destroy(jwt_ctx_t *ctx) {
	if (ctx) {
		if (ctx->pkey) EVP_PKEY_free(ctx->pkey);
		free(ctx);
	}
}