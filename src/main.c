/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>

#include "restconf/sysrepo.h"
#include "restconf/h2c.h"

#if defined(CONFIG_SYSREPO_BUILTIN)
static inline int
init_plugin_sysrepo(sr_session_ctx_t **session __unused,
		    void             **private_data __unused)
{
	sr_session_ctx_t *sess;
	int ret;

	ret = sr_session_start(conn, SR_DS_RUNNING, &sess);
	if (ret)


	ret = sr_plugin_init_cb(sess, private_data);
	if (ret) {

		sess = NULL;
	}

	*session = sess;
	return ret;
}
static inline void
fini_plugin_sysrepo(sr_session_ctx_t *session __unused,
		    void             *private_data __unused)
{
	if (!session)
		return;

	sr_plugin_cleanup_cb(session, private_data);
}
#else
static inline int
init_plugin_sysrepo(sr_session_ctx_t **session __unused,
		    void             **private_data __unused)
{
	return 0;
}
static inline void
fini_plugin_sysrepo(sr_session_ctx_t *session __unused,
		    void             *private_data __unused)
{}
#endif /* CONFIG_SYSREPO_BUILTIN */


static void
usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [options]\n", prog);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -a <addr>   Bind address (default: 127.0.0.1)\n");
	fprintf(stderr, "  -p <port>   Port to listen on (default: 8080)\n");
	fprintf(stderr, "  -u <path>   Listen on Unix socket (h2c) instead of TCP\n");
	fprintf(stderr, "  -d          Run as daemon (background)\n");
	fprintf(stderr, "  -h          Show this help\n");
}

int
main(int argc, char **argv)
{
	const char       *bind_addr = "127.0.0.1";
	uint16_t          port = 8080;
	const char       *uds_path = NULL;
	bool              daemonize = false;
	sr_session_ctx_t *session = NULL;
	void             *private_data = NULL;
	int               ret;

	int opt;
	while ((opt = getopt(argc, argv, "a:p:u:dh")) != -1) {
		switch (opt) {
			case 'a':
				bind_addr = optarg;
				break;
			case 'p':
				port = (uint16_t)atoi(optarg);
				break;
			case 'u':
				uds_path = optarg;
				break;
			case 'd':
				daemonize = true;
				break;
			case 'h':
				usage(argv[0]);
				return 0;
			default:
				usage(argv[0]);
				return 1;
		}
	}

	if (daemonize) {
		if (daemon(0, 0) != 0) {
			perror("daemon");
			return 1;
		}
	}

	ret = init_plugin_sysrepo(&session, &private_data);
	if (ret)
		goto fini_sysrepo;

	// main loop

fini_sysrepo:
	fini_plugin_sysrepo(session, private_data);
	return ret;
}
