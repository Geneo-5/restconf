#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <event2/event.h>
#include "plugin_api.h"
#include "logger.h"


static void sigint_cb(
	evutil_socket_t sig, short events, void *ctx)
{
	(void)sig;
	(void)events;
	struct event_base *base = (struct event_base *)ctx;
	event_base_loopbreak(base);
}

static void print_usage(const char *prog) {
	fprintf(stderr, "Usage: %s [options]\n", prog);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -d          Run as daemon (background)\n");
	fprintf(stderr, "  -s          Use syslog instead of stdout\n");
	fprintf(stderr, "  -v <level>  Runtime log level (0=TRACE, 1=DEBUG, 2=INFO, 3=WARN, 4=ERROR, 5=FATAL)\n");
	fprintf(stderr, "  -h          Show this help\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "IPC socket path (compiled-in): %s\n", PLUGIN_UDS_PATH);
	fprintf(stderr, "  Override at compile time with: -DPLUGIN_UDS_PATH=<path>\n");
}

int main(int argc, char **argv)
{
	bool daemonize = false;
	rc_log_target_t log_target = RC_LOG_TARGET_STDOUT;
	int runtime_log_level = RC_COMPILE_TIME_LOG_LEVEL;

	int opt;
	while ((opt = getopt(argc, argv, "sdv:h")) != -1) {
		switch (opt) {
			case 'd':
				daemonize = true;
				break;
			case 's':
				log_target = RC_LOG_TARGET_SYSLOG;
				break;
			case 'v':
				runtime_log_level = atoi(optarg);
				break;
			case 'h':
				print_usage(argv[0]);
				return 0;
			default:
				print_usage(argv[0]);
				return 1;
		}
	}

	/* Mode daemon : fork en arrière-plan */
	if (daemonize) {
		if (daemon(0, 0) != 0) {
			perror("daemon");
			return 1;
		}
		/* En mode daemon, forcer syslog si stdout était demandé */
		if (log_target == RC_LOG_TARGET_STDOUT) {
			log_target = RC_LOG_TARGET_SYSLOG;
		}
	}

	rc_log_init(log_target, runtime_log_level);

	RC_INFO("Starting External RESTCONF plugin...");

	struct event_base *base = event_base_new();
	if (!base) {
		RC_FATAL("Failed to create event base");
		return 1;
	}

	/* Setup signal handler */
	struct event *sig_event = evsignal_new(
		base, SIGINT, sigint_cb, base);
	event_add(sig_event, NULL);

	/* Connexion sysrepo : ce daemon reutilise plugin_init() de
	 * sysrepo_plugin.c (mode interne), qui fait sr_connect(),
	 * ouvre une session persistante dediee aux abonnements
	 * (ietf-restconf-monitoring, RPC establish-subscription,
	 * notifications restconf-test — cf. ROADMAP.md item "0"),
	 * et cable le pipe d'evenements sysrepo dans libevent. Les
	 * requetes RESTCONF individuelles (GET/POST/PUT/PATCH/
	 * DELETE/RPC) ouvrent chacune leur propre session sysrepo a
	 * courte duree de vie via plugin_handle_get/edit/rpc (voir
	 * open_request_session() dans sysrepo_plugin.c). Le second
	 * parametre (use_external) n'a pas d'effet dans cette
	 * implementation. */
	plugin_ctx_t *sr_ctx = plugin_init(base, false, NULL);
	if (!sr_ctx) {
		RC_FATAL("Failed to connect to sysrepo");
		event_free(sig_event);
		event_base_free(base);
		return 1;
	}

	/* Initialize UDS listener : les requetes recues sur cette
	 * socket sont dispatchees vers plugin_handle_get/edit/rpc
	 * (sysrepo_plugin.c) via sr_ctx (voir uds_plugin.c). */
	if (ext_plugin_init_uds(base, PLUGIN_UDS_PATH, sr_ctx) != 0) {
		RC_FATAL("Failed to init UDS on %s", PLUGIN_UDS_PATH);
		plugin_destroy(sr_ctx);
		event_free(sig_event);
		event_base_free(base);
		return 1;
	}

	RC_INFO("External RESTCONF plugin listening on %s", PLUGIN_UDS_PATH);

	/* Run the single-threaded event loop */
	event_base_dispatch(base);

	/* Cleanup */
	plugin_destroy(sr_ctx);
	event_free(sig_event);
	event_base_free(base);
	RC_INFO("Plugin shutdown complete");
	return 0;
}
