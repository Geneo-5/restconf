/******************************************************************************
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * This file is part of restconf.
 * Copyright (C) 2026 Loic JOURDHEUIL SELLIN
 ******************************************************************************/

#include "restconf/signal.h"
#include <sys/signalfd.h>
#include <stdlib.h>
#include <utils/signal.h>

struct rest_signal {
	struct event_base *base;
	struct event      *event;
	int                fd;
};

static void
dispatch_sigchan(evutil_socket_t fd __unused, short event __unused, void *ctx)
{
	rest_assert(ctx);

	struct rest_signal      *signal = ctx;
	struct signalfd_siginfo  info;
	int                      ret;

	ret = usig_read_fd(signal->fd, &info, 1);
	rest_assert(ret);
	if (ret < 0)
		return;

	switch (info.ssi_signo) {
	case SIGHUP:
	case SIGINT:
	case SIGQUIT:
	case SIGTERM:
		/* Tell caller we were requested to terminate. */
		event_base_loopbreak(signal->base);
		break;

	case SIGUSR1:
	case SIGUSR2:
		/* Silently ignore these... */
		break;

	default:
		rest_assert(0);
	}
}

struct rest_signal *
create_signal(struct event_base  *base)
{
	struct rest_signal *signal;
	sigset_t     msk = *usig_empty_msk;
	sigset_t     blk = *usig_full_msk;

	signal = malloc(sizeof(*signal));
	if (!signal) {
		errno = ENOMEM;
		return NULL;
	}

	signal->base = base;
	usig_addset(&msk, SIGHUP);
	usig_addset(&msk, SIGINT);
	usig_addset(&msk, SIGQUIT);
	usig_addset(&msk, SIGTERM);
	usig_addset(&msk, SIGUSR1);
	usig_addset(&msk, SIGUSR2);
	signal->fd = usig_open_fd(&msk, SFD_NONBLOCK | SFD_CLOEXEC);
	if (signal->fd < 0) {
		errno = -signal->fd;
		goto error;
	}

	signal->event = event_new(base, signal->fd, EV_READ|EV_PERSIST,
	                          dispatch_sigchan, signal);
	if (!signal->event) {
		usig_close_fd(signal->fd);
		goto error;
	}

	usig_delset(&blk, SIGCONT);
	usig_delset(&blk, SIGTSTP);
	usig_delset(&blk, SIGTRAP);
	usig_delset(&blk, SIGTTIN);
	usig_delset(&blk, SIGTTOU);
	usig_procmask(SIG_SETMASK, &blk, NULL);
	return signal;

error:
	free(signal);
	return NULL;
}

void
destroy_signal(struct rest_signal *signal)
{
	if (!signal)
		return;

	event_free(signal->event);
	free(signal);
}

