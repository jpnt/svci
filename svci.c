#include "svci.h"
#include "log.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
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

/* return protocol for run_*() functions:
 *   0   -> child exited 0 (success)
 *  >0   -> child exited with status (raw WEXITSTATUS)
 *  -1   -> fork()/waitpid()/exec failed; errno set by the failing syscall
 */
static int run_sv(const char *cmd, const char *svc) {
	pid_t pid = fork();
	if (pid < 0)
		return -1; /* errno set by fork */

	if (pid == 0) {
		/* child: exec sv <cmd> <svc> */
		execlp("sv", "sv", cmd, svc, (char *)0);
		_exit(127); /* exec failed, parent sees 127 */
	}

	/* parent */
	int status;
	if (waitpid(pid, &status, 0) < 0)
		return -1; /* errno set by waitpid */

	if (WIFEXITED(status))
		return WEXITSTATUS(status);

	/* killed by signal -> treat as backend failure with sentinel >0 */
	return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

svc_rc svc_start(const char *service) {
	log_debug("svc_start: %s\n", service);
	return SVC_OK;
}

svc_rc svc_stop(const char *service) {
	log_debug("svc_stop: %s\n", service);
	return SVC_OK;
}

svc_rc svc_restart(const char *service) {
	log_debug("svc_restart: %s\n", service);
	return SVC_OK;
}

svc_rc svc_list_runit(void) {
	log_debug("svc_list_runit\n");

	struct dirent *ent;
	char path[PATH_MAX];
	struct stat st;
	int r;

	const char *home = getenv("HOME");
	if (home && *home) {
		char userdir[PATH_MAX];

		if (snprintf(userdir, sizeof(userdir), "%s/.config/service",
			     home) < (int)sizeof(userdir)) {
			DIR *d = opendir(userdir);
			if (d) {
				while ((ent = readdir(d)) != NULL) {

					if (ent->d_name[0] == '.')
						continue;

					if (snprintf(path, sizeof(path),
						     "%s/%s", userdir,
						     ent->d_name) >=
					    (int)sizeof(path))
						continue;

					if (lstat(path, &st) < 0)
						continue;

					if (!S_ISDIR(st.st_mode) &&
					    !S_ISLNK(st.st_mode))
						continue;

					r = run_sv("status", path);

					if (r == -1) {
						fprintf(stderr,
							"svci: %s: %s\n", path,
							strerror(errno));
						closedir(d);

						return (errno == EACCES ||
							errno == EPERM)
							   ? SVC_ERR_PERMISSION
							   : SVC_ERR_IO;
					}

					if (r != 0) {
						fprintf(stderr,
							"svci: backend error "
							"for %s (exit %d)\n",
							path, r);
						closedir(d);
						return SVC_ERR_BACKEND;
					}
				}
				closedir(d);
			}
		}
	}

	if (geteuid() != 0) {
		// non-root
		return SVC_OK;
	}

	DIR *d = opendir("/var/service");
	if (!d) {
		fprintf(stderr, "svci: /var/service: %s\n", strerror(errno));

		return (errno == EACCES || errno == EPERM) ? SVC_ERR_PERMISSION
							   : SVC_ERR_IO;
	}

	while ((ent = readdir(d)) != NULL) {

		if (ent->d_name[0] == '.')
			continue;

		if (snprintf(path, sizeof(path), "/var/service/%s",
			     ent->d_name) >= (int)sizeof(path))
			continue;

		if (lstat(path, &st) < 0)
			continue;

		if (!S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode))
			continue;

		r = run_sv("status", path);

		if (r == -1) {
			fprintf(stderr, "svci: %s: %s\n", path,
				strerror(errno));
			closedir(d);

			return (errno == EACCES || errno == EPERM)
				   ? SVC_ERR_PERMISSION
				   : SVC_ERR_IO;
		}

		if (r != 0) {
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
		"    start <svc>    start service\n"
		"    stop <svc>     stop service\n"
		"    restart <svc>  restart service\n"
		"    list           list services\n",
		prog);
	exit(1);
}

int main(int argc, char *argv[]) {
	if (argc < 2)
		usage(argv[0]);

	const char *cmd = argv[1];

	if (parse_pid1() != 0)
		return 1;

	if (strcmp(cmd, "start") == 0) {
		if (argc != 3)
			usage(argv[0]);
		return svc_start(argv[2]) == SVC_OK ? 0 : 1;
	}

	if (strcmp(cmd, "stop") == 0) {
		if (argc != 3)
			usage(argv[0]);
		return svc_stop(argv[2]) == SVC_OK ? 0 : 1;
	}

	if (strcmp(cmd, "restart") == 0) {
		if (argc != 3)
			usage(argv[0]);
		return svc_restart(argv[2]) == SVC_OK ? 0 : 1;
	}

	if (strcmp(cmd, "list") == 0) {
		if (argc != 2)
			usage(argv[0]);
		return svc_list() == SVC_OK ? 0 : 1;
	}

	usage(argv[0]); /* fallthrough */
	return 0;	/* not reached */
}
