#include "svci.h"
#include "log.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PID1_COMM_LEN 32 /* > TASK_COMM_LEN (16), safe margin */

typedef enum {
	PID1_UNKNOWN = 0,
	PID1_RUNIT,
	PID1_SYSTEMD,
	PID1_OPENRC,
	PID1_S6,
	PID1_SYSVINIT,
	PID1_BUSYBOX,
} pid1_type_t;

pid1_type_t g_pid1 = PID1_UNKNOWN;

/* static command table */
struct cmd {
	const char *name;
	int argc_needed;
	int (*fn)(int argc, char **argv);
};
static int do_start(int argc, char **argv) {
	(void)argc;
	return svc_start(argv[2]);
}
static int do_stop(int argc, char **argv) {
	(void)argc;
	return svc_stop(argv[2]);
}
static int do_restart(int argc, char **argv) {
	(void)argc;
	return svc_restart(argv[2]);
}
static int do_list(int argc, char **argv) {
	(void)argc;
	(void)argv;
	return svc_list();
}

static struct cmd cmdtab[] = {
    {"start", 3, do_start},
    {"stop", 3, do_stop},
    {"restart", 3, do_restart},
    {"list", 2, do_list},
};
static const size_t ncmd = sizeof(cmdtab) / sizeof(cmdtab[0]);

typedef int (*svc_entry_cb)(const char *fullpath);
/* wrappers */
static int cb_runit_status(const char *path) {
	char *argv[] = {"sv", "status", (char *)path, NULL};
	return run_cmd(argv);
}

static svc_rc walk_dir(const char *dir, svc_entry_cb cb) {
	DIR *d = opendir(dir);
	if (!d)
		return (errno == EACCES || errno == EPERM) ? SVC_ERR_PERMISSION
							   : SVC_ERR_IO;

	struct dirent *ent;
	struct stat st;
	char path[PATH_MAX];

	while ((ent = readdir(d)) != NULL) {

		if (ent->d_name[0] == '.')
			continue;

		if (snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name) >=
		    (int)sizeof(path))
			continue;

		if (lstat(path, &st) < 0)
			continue;

		if (!S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode))
			continue;

		int r = cb(path);

		if (r == -1) {
			fprintf(stderr, "svci: %s: %s\n", path,
				strerror(errno));
			closedir(d);
			return SVC_ERR_IO;
		}

		if (r > 0) {
			fprintf(stderr,
				"svci: backend error for %s (exit %d)\n", path,
				r);
			closedir(d);
			return SVC_ERR_BACKEND;
		}
	}

	closedir(d);
	return SVC_OK;
}

static int get_user_service_dir(char *out, size_t outsz) {
	const char *home = getenv("HOME");
	if (!home || !*home)
		return 0;

	int n = snprintf(out, outsz, "%s/.config/service", home);
	if (n < 0 || n >= (int)outsz)
		return 0;

	return 1;
}

svc_rc svc_start(const char *service) {
	log_debug("svc_start: %s\n", service);
	(void)service;
	return SVC_ERR_UNSUPPORTED;
}

svc_rc svc_stop(const char *service) {
	log_debug("svc_stop: %s\n", service);
	(void)service;
	return SVC_ERR_UNSUPPORTED;
}

svc_rc svc_restart(const char *service) {
	log_debug("svc_restart: %s\n", service);
	(void)service;
	return SVC_ERR_UNSUPPORTED;
}

svc_rc svc_list_runit(void) {
	log_debug("svc_list_runit");

	char userdir[PATH_MAX];

	/* always try user service*/
	if (get_user_service_dir(userdir, sizeof(userdir)))
		walk_dir(userdir, cb_runit_status);

	/* only root sees system services */
	/* the kernel checks effective UID for permission */
	if (geteuid() != 0)
		return SVC_OK;

	return walk_dir("/var/service", cb_runit_status);
}

svc_rc svc_list(void) {
	log_debug("svc_list\n");

	switch (g_pid1) {
	case PID1_RUNIT:
		return svc_list_runit();
	case PID1_SYSTEMD:
	case PID1_OPENRC:
	case PID1_S6:
	case PID1_SYSVINIT:
	case PID1_BUSYBOX:
	case PID1_UNKNOWN:
	default:
		return SVC_ERR_UNSUPPORTED;
	}
}

static inline svc_rc svc_map_errno(int e) {
	switch (e) {
	case EINVAL:
		return SVC_ERR_INVALID;
	case ENOENT:
		return SVC_ERR_NOTFOUND;
	case EACCES:
	case EPERM:
		return SVC_ERR_PERMISSION;
	default:
		return SVC_ERR_UNKNOWN;
	}
}

static int parse_pid1(void) {
	char buf[PID1_COMM_LEN];
	FILE *f = fopen("/proc/1/comm", "r");
	if (!f)
		return -1;

	if (!fgets(buf, sizeof(buf), f)) {
		fclose(f);
		return -1;
	}
	fclose(f);

	buf[strcspn(buf, "\n")] = '\0';

	log_debug("parsed pid1: %s\n", buf);

	if (strcmp(buf, "systemd") == 0)
		g_pid1 = PID1_SYSTEMD;
	else if (strcmp(buf, "runit") == 0)
		g_pid1 = PID1_RUNIT;
	else if (strcmp(buf, "s6-svscan") == 0)
		g_pid1 = PID1_S6;
	else if (strcmp(buf, "openrc-init") == 0)
		g_pid1 = PID1_OPENRC;
	else if (strcmp(buf, "init") == 0) {
		// TODO: detect further
	} else
		g_pid1 = PID1_UNKNOWN;

	return 0;
}

static void usage(const char *prog) {
	fprintf(stderr,
		"usage: %s <command> [args]\n"
		"commands:\n"
		"    enable <svc>   enable service\n"
		"    disable <svc>  disable service\n"
		"    start <svc>    start service\n"
		"    stop <svc>     stop service\n"
		"    restart <svc>  restart service\n"
		"    list           list services\n",
		prog);
	exit(1);
}

static void dispatch(int argc, char **argv) {
	const char *input = argv[1];
	size_t matches = 0;
	size_t idx = 0;

	for (size_t i = 0; i < ncmd; i++) {
		if (strncmp(input, cmdtab[i].name, strlen(input)) == 0) {
			matches++;
			idx = i;
		}
	}

	if (matches == 0) {
		fprintf(stderr, "svci: unknown command '%s'\n", input);
		usage(argv[0]);
	}

	if (matches > 1) {
		fprintf(stderr, "svci: ambiguous command '%s'\n", input);
		usage(argv[0]);
	}

	/* Exactly one match now: cmdtab[idx] */
	if (argc != cmdtab[idx].argc_needed) {
		fprintf(stderr, "svci: missing service argument\n");
		usage(argv[0]);
	}

	int rc = cmdtab[idx].fn(argc, argv);
	exit(rc == SVC_OK ? 0 : 1);
}

int main(int argc, char *argv[]) {
	if (argc < 2)
		usage(argv[0]);

	if (parse_pid1() != 0)
		return 1;

	dispatch(argc, argv);

	return 0; /* not reached */
}
