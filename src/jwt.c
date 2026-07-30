/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#include "restconf/h2c.h"
#include <jwt.h>

static int
jwt_bearer_cb(jwt_t *jwt, jwt_config_t *config)
{
	char **user = config->ctx;
	jwt_value_t value;


	jwt_set_GET_STR(&value, CONFIG_JWT_GRANT_USERNAME);
	if (jwt_claim_get(jwt, &value) != JWT_VALUE_ERR_NONE)
		return -EINVAL;

	if (jwt_get_alg(jwt) != JWT_ALG_NONE)
		return -EINVAL;

	if (value.str_val == NULL || value.str_val[0] == '\0')
		return -EINVAL;

	*user = strdup(value.str_val);
	return *user ? 0 : -ENOMEM;
}

#if defined(CONFIG_ALG_NONE)
static int
jwt_checker_config_key(jwt_checker_t *checker)
{
	static const jwt_alg_t allowed_algs[] = {
		JWT_ALG_NONE
	};

	return jwt_checker_setalgs(checker, allowed_algs, stroll_array_nr(allowed_algs));
}
#else
/*
	ret = jwt_checker_setkey(checker, JWT_ALG_RS256, jwt_verification_key);
	if (ret)
		return -1;
*/
#error JWT Algo not supported
#endif

int
jwt_bearer_token_get_name(const char *bearer, char **user)
{
	rest_assert(bearer);
	rest_assert(user);

	const char  *token;
	jwt_checker_auto_t *checker = NULL;

	int ret;

	if (strncmp(bearer, "Bearer ", 7) != 0)
		return -EINVAL;

	token = bearer + 7;
	if (token[0] == '\0')
		return -EINVAL;

	checker = jwt_checker_new();
	if (!checker)
		return -ENOMEM;


	ret = jwt_checker_config_key(checker);
	if (ret)
		return ret;

	ret = jwt_checker_setcb(checker, jwt_bearer_cb, user);
	if (ret)
		return -ENOMEM;

	ret = jwt_checker_verify(checker, token);
	if (ret && *user) {
		free(*user);
		*user = NULL;
	}
	return ret;
}
