#define _POSIX_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <err.h>
#include <stdio.h>

#define EXTERNAL_FAIL()	failures++; time(&last_fail); continue
#define INTERNAL_FAIL(what)	warn(what); failures++; time(&last_fail); continue

extern char **environ;

volatile int got_sigwinch = 0;

void
handle_sigwinch(int sig) {
	assert(sig == SIGWINCH);
	got_sigwinch = 1;
}

int
main(int argc, char **argv) {
	pid_t pid = 0, wpid;
	int status;
	int failures = 0;
	time_t last_fail = 0;

	signal(SIGWINCH, handle_sigwinch);

	while(1) {
		if(failures > 0) {
			time_t now;
			time(&now);
			if(now - last_fail > 300) {
				failures = 0;
			}
			if(failures >= 5) {
				system("reboot");
			}
		}

		if(pid <= 0) {
			pid = fork();
			if(pid == 0) {
				execve("./dmxmain", argv, environ);
				err(1, "execve");
			} else if(pid == -1) {
				INTERNAL_FAIL("fork");
				pid = 0;
			}
		}

		wpid = waitpid(pid, &status, WNOHANG);
		if(wpid == -1) {
			INTERNAL_FAIL("waitpid");
		} else if(wpid == pid) {
			if(WIFEXITED(status)) {
				fprintf(stderr, "dmxd exited with exit code %d\n", WEXITSTATUS(status));
			} else if(WIFSIGNALED(status)) {
				fprintf(stderr, "dmxd was killed by signal %d\n", WTERMSIG(status));
			}
			EXTERNAL_FAIL();
			pid = 0;
		} else {
			INTERNAL_FAIL("not reached");
		}

		got_sigwinch = 0;
		sleep(30);

		if(!got_sigwinch) {
			kill(pid, 15);
			sleep(3);
			kill(pid, 9);
			EXTERNAL_FAIL();
		}
	}
}
