#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * Minimal POSIX command runner. No policy and overhead.
 *
 * run_cmd:
 *
 *  argv must be a NULL-terminated array:
 *      argv[0] = program name
 *      argv[1] = arg1
 *      ...
 *      argv[n] = NULL
 *
 *  return values:
 *      0      child exited 0
 *      >0     child exited with that status
 *     -1      fork/exec/waitpid failed (errno is set)
 */
int run_cmd(char *const argv[]) {
	if (!argv || !argv[0]) {
		errno = EINVAL;
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0)
		return -1; /* fork failed, errno set */

	if (pid == 0) {
		/* child */
		execvp(argv[0], argv);
		_exit(127); /* exec failed, parent sees 127 */
	}

	/* parent */
	int status;
	if (waitpid(pid, &status, 0) < 0)
		return -1; /* waitpid failed */

	if (WIFEXITED(status))
		return WEXITSTATUS(status);

	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status); /* standard shell convention */

	/* unreachable on POSIX, but kept for completeness */
	return 255;
}
