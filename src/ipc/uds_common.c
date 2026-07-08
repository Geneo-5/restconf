#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc/uds_common.h"

int ipc_serialize_message(
	ipc_msg_type_t type,
	uint32_t msg_id,
	int32_t status_code,
	const uint8_t *payload,
	size_t payload_len,
	uint8_t **out_buf,
	size_t *out_len)
{
	size_t total = sizeof(ipc_msg_header_t) + payload_len;
	uint8_t *buf = malloc(total);
	if (!buf) return -1;

	ipc_msg_header_t *hdr = (ipc_msg_header_t *)buf;
	hdr->magic = IPC_MAGIC_NUMBER;
	hdr->msg_id = msg_id;
	hdr->type = type;
	hdr->payload_len = (uint32_t)payload_len;
	hdr->status_code = status_code;

	if (payload && payload_len > 0) {
		memcpy(buf + sizeof(ipc_msg_header_t),
		       payload, payload_len);
	}

	*out_buf = buf;
	*out_len = total;
	return 0;
}

int ipc_serialize_request(
	ipc_msg_type_t type,
	uint32_t msg_id,
	const uint8_t *payload,
	size_t payload_len,
	uint8_t **out_buf,
	size_t *out_len)
{
	return ipc_serialize_message(
		type, msg_id, 0, payload, payload_len,
		out_buf, out_len);
}

int ipc_parse_header(
	const uint8_t *buf,
	size_t len,
	ipc_msg_header_t *header_out)
{
	if (len < sizeof(ipc_msg_header_t)) {
		return -1;
	}
	memcpy(header_out, buf, sizeof(ipc_msg_header_t));
	if (header_out->magic != IPC_MAGIC_NUMBER) {
		return -1;
	}
	return 0;
}
