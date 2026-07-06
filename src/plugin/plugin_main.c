#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <event2/event.h>
#include <sysrepo.h>

/* Forward declaration from uds_plugin.c */
extern int ext_plugin_init_uds(
	struct event_base *base, const char *path);

static void sigint_cb(
	evutil_socket_t sig, short events, void *ctx)
{
	struct event_base *base = (struct event_base *)ctx;
	event_base_loopbreak(base);
}

int main(int argc, char **argv)
{
	const char *uds_path = "/var/run/restconf-plugin.sock";
	if (argc > 1) {
		uds_path = argv[1];
	}

	struct event_base *base = event_base_new();
	if (!base) {
		fprintf(stderr, "Failed to create event base\n");
		return 1;
	}

	/* Setup signal handler */
	struct event *sig_event = evsignal_new(
		base, SIGINT, sigint_cb, base);
	event_add(sig_event, NULL);

	/* Initialize sysrepo connection here */
	/* TODO: sr_connect, sr_session_start */

	/* Initialize UDS listener */
	if (ext_plugin_init_uds(base, uds_path) != 0) {
		fprintf(stderr, "Failed to init UDS\n");
		return 1;
	}

	printf("External RESTCONF plugin listening on %s\n",
	       uds_path);

	/* Run the single-threaded event loop */
	event_base_dispatch(base);

	/* Cleanup */
	event_free(sig_event);
	event_base_free(base);
	return 0;
}
