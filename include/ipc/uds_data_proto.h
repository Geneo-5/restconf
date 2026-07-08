#ifndef UDS_DATA_PROTO_H
#define UDS_DATA_PROTO_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
 * Format binaire prive utilise entre restconf-server (gateway) et
 * restconf-plugin (daemon externe) pour transporter les champs de
 * rc_request_t necessaires aux operations GET/EDIT/RPC (voir
 * uds_gateway.c et uds_plugin.c).
 *
 * Toutes les chaines sont encodees comme : uint32_t longueur (0 si
 * absente/NULL) suivi des octets bruts (sans terminateur nul). Les
 * corps de requete (POST/PUT/PATCH, input RPC) suivent le meme
 * schema via uds_proto_put_bytes()/uds_proto_get_bytes_ref().
 *
 * Ce format n'est PAS un protocole public : les deux extremites sont
 * compilees depuis le meme depot et doivent rester synchronisees.
 * Toutes les fonctions de lecture verifient les bornes du buffer
 * source et retournent -1 en cas de depassement.
 */

static inline int uds_proto_put_u32(
	uint8_t *buf, size_t cap, size_t *pos, uint32_t val)
{
	if (*pos + sizeof(val) > cap)
		return -1;
	memcpy(buf + *pos, &val, sizeof(val));
	*pos += sizeof(val);
	return 0;
}

static inline int uds_proto_put_i32(
	uint8_t *buf, size_t cap, size_t *pos, int32_t val)
{
	uint32_t uval;
	memcpy(&uval, &val, sizeof(uval));
	return uds_proto_put_u32(buf, cap, pos, uval);
}

static inline int uds_proto_put_u8(
	uint8_t *buf, size_t cap, size_t *pos, uint8_t val)
{
	if (*pos + 1 > cap)
		return -1;
	buf[*pos] = val;
	*pos += 1;
	return 0;
}

static inline int uds_proto_put_str(
	uint8_t *buf, size_t cap, size_t *pos, const char *str)
{
	uint32_t len = str ? (uint32_t)strlen(str) : 0;

	if (uds_proto_put_u32(buf, cap, pos, len) != 0)
		return -1;
	if (len > 0) {
		if (*pos + len > cap)
			return -1;
		memcpy(buf + *pos, str, len);
		*pos += len;
	}
	return 0;
}

static inline int uds_proto_put_bytes(
	uint8_t *buf, size_t cap, size_t *pos,
	const uint8_t *data, uint32_t len)
{
	if (uds_proto_put_u32(buf, cap, pos, len) != 0)
		return -1;
	if (len > 0) {
		if (!data || *pos + len > cap)
			return -1;
		memcpy(buf + *pos, data, len);
		*pos += len;
	}
	return 0;
}

static inline int uds_proto_get_u32(
	const uint8_t *buf, size_t len, size_t *pos, uint32_t *out)
{
	if (*pos + sizeof(*out) > len)
		return -1;
	memcpy(out, buf + *pos, sizeof(*out));
	*pos += sizeof(*out);
	return 0;
}

static inline int uds_proto_get_i32(
	const uint8_t *buf, size_t len, size_t *pos, int32_t *out)
{
	uint32_t uval;

	if (uds_proto_get_u32(buf, len, pos, &uval) != 0)
		return -1;
	memcpy(out, &uval, sizeof(uval));
	return 0;
}

static inline int uds_proto_get_u8(
	const uint8_t *buf, size_t len, size_t *pos, uint8_t *out)
{
	if (*pos + 1 > len)
		return -1;
	*out = buf[*pos];
	*pos += 1;
	return 0;
}

/*
 * Retourne une chaine allouee (malloc), a liberer par l'appelant via
 * free(). *out_str vaut NULL si la longueur encodee est 0.
 */
static inline int uds_proto_get_str(
	const uint8_t *buf, size_t len, size_t *pos, char **out_str)
{
	uint32_t slen;
	char *s;

	if (uds_proto_get_u32(buf, len, pos, &slen) != 0)
		return -1;
	if (slen == 0) {
		*out_str = NULL;
		return 0;
	}
	if (*pos + slen > len)
		return -1;

	s = malloc((size_t)slen + 1);
	if (!s)
		return -1;
	memcpy(s, buf + *pos, slen);
	s[slen] = '\0';
	*pos += slen;
	*out_str = s;
	return 0;
}

/*
 * Retourne une reference (PAS une copie) vers les octets bruts a
 * l'interieur de @p buf, valide tant que @p buf n'est pas libere.
 */
static inline int uds_proto_get_bytes_ref(
	const uint8_t *buf, size_t len, size_t *pos,
	const uint8_t **out_data, uint32_t *out_len)
{
	uint32_t blen;

	if (uds_proto_get_u32(buf, len, pos, &blen) != 0)
		return -1;
	if (*pos + blen > len)
		return -1;

	*out_data = (blen > 0) ? (buf + *pos) : NULL;
	*out_len = blen;
	*pos += blen;
	return 0;
}

#endif /* UDS_DATA_PROTO_H */
