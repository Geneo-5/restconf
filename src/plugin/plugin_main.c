#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <event2/event.h>
#include <sysrepo.h>
#include "logger.h"

/* Forward declaration from uds_plugin.c */
extern int ext_plugin_init_uds(
	struct event_base *base, const char *path);

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

	/* Initialize sysrepo connection here */
	/* TODO: sr_connect, sr_session_start */

	/* Initialize UDS listener */
	if (ext_plugin_init_uds(base, PLUGIN_UDS_PATH) != 0) {
		RC_FATAL("Failed to init UDS on %s", PLUGIN_UDS_PATH);
		return 1;
	}

	RC_INFO("External RESTCONF plugin listening on %s", PLUGIN_UDS_PATH);

	/* Run the single-threaded event loop */
	event_base_dispatch(base);

	/* Cleanup */
	event_free(sig_event);
	event_base_free(base);
	RC_INFO("Plugin shutdown complete");
	return 0;
}
