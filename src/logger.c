#define _GNU_SOURCE
#include "logger.h"
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <syslog.h>
#include <stdlib.h>

static rc_log_target_t g_log_target = RC_LOG_TARGET_STDOUT;
static int g_runtime_level = RC_COMPILE_TIME_LOG_LEVEL;

static const char *level_strings[] = {
	"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static int level_to_syslog[] = {
	LOG_DEBUG, LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERR, LOG_CRIT
};

void
rc_log_init(rc_log_target_t target, int runtime_level)
{
	g_log_target = target;

	/* Runtime verbosity cannot exceed compile-time level. */
	if (runtime_level < RC_COMPILE_TIME_LOG_LEVEL) {
		g_runtime_level = RC_COMPILE_TIME_LOG_LEVEL;
	} else if (runtime_level > RC_LOG_FATAL) {
		g_runtime_level = RC_LOG_FATAL;
	} else {
		g_runtime_level = runtime_level;
	}

	if (g_log_target == RC_LOG_TARGET_SYSLOG) {
		openlog("restconf-server", LOG_PID | LOG_NDELAY,
		        LOG_DAEMON);
	}
}

void
rc_log_set_level(int level)
{
	if (level < RC_COMPILE_TIME_LOG_LEVEL) {
		g_runtime_level = RC_COMPILE_TIME_LOG_LEVEL;
	} else if (level > RC_LOG_FATAL) {
		g_runtime_level = RC_LOG_FATAL;
	} else {
		g_runtime_level = level;
	}
}

void
rc_log_print(int level, const char *file, int line,
	const char *fmt, ...)
{
	if (level < g_runtime_level) {
		return;
	}

	va_list args;
	va_start(args, fmt);

	/* Extract just the filename from __FILE__ */
	const char *base = strrchr(file, '/');
	base = base ? base + 1 : file;

	if (g_log_target == RC_LOG_TARGET_SYSLOG) {
		char buf[2048];

		vsnprintf(buf, sizeof(buf), fmt, args);
		syslog(level_to_syslog[level], "[%s] %s:%d - %s",
		       level_strings[level], base, line, buf);
	} else {
		time_t t = time(NULL);
		struct tm tm;

		localtime_r(&t, &tm);

		char time_buf[20];

		strftime(time_buf, sizeof(time_buf),
		         "%Y-%m-%d %H:%M:%S", &tm);

		fprintf(stderr, "%s [%-5s] %s:%d - ",
		        time_buf, level_strings[level], base, line);
		vfprintf(stderr, fmt, args);
		fprintf(stderr, "\n");
		fflush(stderr);
	}

	va_end(args);
}
