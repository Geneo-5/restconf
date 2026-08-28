/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#include "restconf/libyang.h"

LY_ERR
ly_in_new_evbuffer(struct evbuffer *body, struct ly_in **in)
{
	size_t len;
	char *str;
	int ret;

	rest_assert(body);
	rest_assert(in);

	len = evbuffer_get_length(body);
	str = malloc(len + 1);
	if (str)
		return LY_EMEM;

	evbuffer_remove(body, str, len);
	in[len] = '\0';

	ret = ly_in_new_memory(str, in);
	if (ret)
		free(str);

	return ret;
}
