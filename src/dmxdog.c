#define _POSIX_SOURCE
#define _BSD_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <err.h>
#include <stdio.h>
#include <sysexits.h>
#include <getopt.h>

#define EXTERNAL_FAIL()	failures++; time(&last_fail); continue
#define INTERNAL_FAIL(what)	warn(what); failures++; time(&last_fail); continue

extern char **environ;
extern char *optarg;
extern int optind, opterr, optopt;

volatile int got_sigwinch = 0;

void
handle_sigwinch(int sig) {
	assert(sig == SIGWINCH);
	got_sigwinch = 1;
}

int
main(int argc, char **argv) {
	pid_t pid = 0, wpid;
	int opt, status;
	int failures = 0;
	time_t last_fail = 0;

	int daemonize = 0;

	signal(SIGWINCH, handle_sigwinch);

	while((opt = getopt(argc, argv, "Dc:")) != -1) {
		switch(opt) {
			case 'D':
				daemonize = 1;
				break;
			case 'c':
				if(chdir(optarg) == -1) {
					warn("chdir");
				}
				break;
			default:
				fprintf(stderr, "Usage: %s [-c chdir] [-D]\n", argv[0]);
				exit(EX_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if(daemonize) {
		if(daemon(1, 0) == -1) {
			warn("daemon");
		}
	}

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
				pid = 0;
				INTERNAL_FAIL("fork");
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
			pid = 0;
			EXTERNAL_FAIL();
		}

		got_sigwinch = 0;
		sleep(30);

		if(!got_sigwinch) {
			kill(pid, 15);
			sleep(3);
			kill(pid, 9);
		}
	}
}
