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
#include <grp.h>

#include "restconf/sysrepo.h"
#include "restconf/h2c.h"
#include "restconf/signal.h"

#if defined(CONFIG_SYSREPO_BUILTIN)
static inline int
init_plugin_sysrepo(sr_conn_ctx_t     *conn,
                    sr_session_ctx_t **session,
                    void             **private_data)
{
	sr_session_ctx_t *sess;
	int ret;

	ret = sr_session_start(conn, SR_DS_RUNNING, &sess);
	if (ret)
		return ret;

	ret = sr_plugin_init_cb(sess, private_data);
	if (ret) {
		sr_session_stop(sess);
		sess = NULL;
	}

	*session = sess;
	return ret;
}
static inline void
fini_plugin_sysrepo(sr_session_ctx_t *session,
                    void             *private_data)
{
	if (!session)
		return;

	sr_plugin_cleanup_cb(session, private_data);
}
#else
static inline int
init_plugin_sysrepo(sr_conn_ctx_t     *conn __unused,
                    sr_session_ctx_t **session __unused,
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
	fprintf(stderr, "  -g <group>  Set Unix socket group permission\n");
	fprintf(stderr, "  -d          Run as daemon (background)\n");
	fprintf(stderr, "  -t <sec>    Idle read timeout per connection, 0=disabled (default: 300)\n");
	fprintf(stderr, "  -h          Show this help\n");
}

int
main(int argc, char **argv)
{
	const char         *bind_addr = "127.0.0.1";
	uint16_t            port = 8080;
	gid_t               gid = (gid_t)-1;
	const char         *uds_path = NULL;
	bool                daemonize = false;
	int                 idle_timeout_sec = 300;
	struct event_base  *base;
	sr_conn_ctx_t      *conn = NULL;
	sr_session_ctx_t   *session = NULL;
	void               *sr_priv = NULL;
	struct rest_server *server = NULL;
	struct rest_signal *signal = NULL;
	int                 ret;

	int opt;
	while ((opt = getopt(argc, argv, "a:p:u:g:dt:h")) != -1) {
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
			case 'g':
				struct group *group = getgrnam(optarg);

				if (!group) {
					fprintf(stderr, "Bad group name %s\n", optarg);
					usage(argv[0]);
					return 1;
				}
				gid = group->gr_gid;
				break;
			case 'd':
				daemonize = true;
				break;
			case 't':
				idle_timeout_sec = atoi(optarg);
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

	base = event_base_new();
	signal = create_signal(base);
	if (!signal) {
		ret = -errno;
		goto end;
	}

	ret = sr_connect(SR_CONN_DEFAULT, &conn);
	if (ret)
		goto end;

	ret = init_plugin_sysrepo(conn, &session, &sr_priv);
	if (ret)
		goto end;

	if (uds_path)
		server = create_uds_server(base, conn, uds_path, gid);
	else
		server = create_tcp_server(base, conn, bind_addr, port);
	if (!server) {
		ret = -errno;
		goto end;
	}

	server_set_idle_timeout(server, idle_timeout_sec);
	event_base_dispatch(base);
end:
	destroy_server(server);
	fini_plugin_sysrepo(session, sr_priv);
	if (conn)
		sr_disconnect(conn);
	destroy_signal(signal);
	event_base_free(base);
	return ret;
}
