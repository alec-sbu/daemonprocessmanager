#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <wait.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "event.h"
#include "driver.h"
#include "tracker.h"
#include "debug.h"

/*
 * Maximum number of jobs we can create in one test script.
 */
#define JOBIDS_MAX 100

/*
 * Maximum size of buffer for command to be sent.
 */
#define SEND_MAX 200

char *event_type_names[] = {
	[NO_EVENT]		"NONE",
	[ANY_EVENT]		"ANY",
	[EOF_EVENT]		"EOF",
	[INIT_EVENT]		"INIT",
	[FINI_EVENT]		"FINI",
	[REGISTER_EVENT]	"REGISTER",
	[UNREGISTER_EVENT]	"UNREGISTER",
	[START_EVENT]   	"START",
	[ACTIVE_EVENT]		"ACTIVE",
	[STOP_EVENT]		"STOP",
	[KILL_EVENT]		"KILL",
	[RESET_EVENT]		"RESET",
	[LOGROTATE_EVENT]	"LOGROTATE",
	[TERM_EVENT]		"TERM",
	[CRASH_EVENT]		"CRASH",
	[ERROR_EVENT]		"ERROR",
	[PROMPT_EVENT]		"PROMPT",
	[STATUS_EVENT]		"STATUS"
};

static int setup_socket(char *name);
static FILE *run_target(char *prog, char *av[], int *pidp);
static EVENT *read_event(int fd, struct timeval *timeout);

int run_test(char *name, char *target, char *av[], COMMAND *script, int *statusp) {
    fprintf(stderr, "Running test %s\n", name);
    signal(SIGPIPE, SIG_IGN);  // Avoid embarrassing death
    int listen_fd = setup_socket(EVENT_SOCKET);
    int client_fd = 0;
    int pid = 0;
    FILE *cmd_str = run_target(target, av, &pid);

    // Main loop:
    EVENT *ep;
    COMMAND *scrp = script;
    while(scrp->send || scrp->expect) {
	if(cmd_str && scrp->send) {
	    if(scrp->send == SEND_EOT) {
		// Close command stream to child process.
		fprintf(stderr, ">>> EOT\n");
		fclose(cmd_str);
		cmd_str = NULL;
	    } else {
		// Send next command to child process.
		fprintf(stderr, ">>> %s\n", scrp->send);
		fprintf(cmd_str, "%s\n", scrp->send);
		fflush(cmd_str);
	    }
	}
	if(scrp->expect) {
	    fprintf(stderr, "                   [expect %s]\n", event_type_names[scrp->expect]);
	    if(scrp->expect == EOF_EVENT) {
		// If we are expecting EOF, then we want to check to make sure there are no
		// unread events.  This is tricky, because we don't want to block waiting
		// for a client connection or waiting to read an event.  So we have to set
		// non-blocking I/O both on the client connection, but also on the server socket,
		// because we might not yet have accepted any connection from the client.
		// To be certain that the child has already finished, rather than just slow
		// here we wait for the termination of the child.  This way, when we set
		// O_NONBLOCK on the server socket a failure of accept() indicates that no
		// connection request will ever be forthcoming.
		if(cmd_str) {
		    fclose(cmd_str);
		    cmd_str = NULL;
		}
		if(pid == waitpid(-1, statusp, 0))
		    pid = 0;
		// Now we know the child has actually finished, and either there is a
		// connection, possibly with events left on it, or there is no connection
		// and there never will be.  Set O_NONBLOCK so we don't wait for a connection.
		fcntl(listen_fd, F_SETFL, O_NONBLOCK);
	    }
	    if(!client_fd) {
		// The specification of the client event interface is deficient in that it
		// does not have an initialization function that can be used to immediately
		// create a connection.  So we have to delay accepting a connection from the
		// client until the first time we expect a response.
		fprintf(stderr, "Waiting for connection...\n");
		alarm(1);
		if((client_fd = accept(listen_fd, NULL, NULL)) < 0) {
		    alarm(0);
		    if(scrp->expect == EOF_EVENT) {
			// This is what is expected to happen.
			fprintf(stderr, "End of test %s\n", name);
			return 0;
		    } else {
			// This should probably not happen.  Treat it as a failure.
			perror("accept");
			goto abort_test;
		    }
		}
                alarm(0);
		fprintf(stderr, "Connected to child: fd = %d\n", client_fd);
	    }
	    // Check for any leftover events on the connection.
	    if(scrp->expect == EOF_EVENT) {
		fcntl(client_fd, F_SETFL, O_NONBLOCK);
		if((ep = read_event(client_fd, &scrp->timeout)) != NULL) {
		    fprintf(stderr, "Event type %s read when expecting EOF\n",
			    event_type_names[ep->type]);
		    goto abort_test;
		} else {
		    break;
		}
	    }
	    // Read event and check whether it matches what is expected.
	    while((ep = read_event(client_fd, &scrp->timeout)) != NULL) {
	      int matched = (scrp->expect == ANY_EVENT || ep->type == scrp->expect);
	      // If event matched expected, check pre-transition assertion, if any.
	      if(matched && scrp->before)
		scrp->before(ep, NULL, scrp->args);
	      // Perform transition regardless of whether event matched or not.
	      if(track_event(ep) == -1) {
		fprintf(stderr, "Event tracker returned an error\n");
		goto abort_test;
	      }
	      // If event matched expected, check post-transition assertion, if any.
	      if(matched && scrp->after)
		scrp->after(ep, NULL, scrp->args);
	      if(ep == NULL) {
		// Seeing EOF before the end of the script is an error.
		if(scrp->expect != EOF_EVENT) {
		  fprintf(stderr, "Unexpected EOF event\n");
		  goto abort_test;
		}
	      } else if(matched) {
		// If event matched expected, then advance test script.
		// fprintf(stderr, "Event type %s matched expected type %s\n",
		//	   event_type_names[ep->type], event_type_names[scrp->expect]);
		break;
	      } else if(scrp->modifiers & EXPECT_SKIP_OTHER) {
		// If event did not match expected, but EXPECT_SKIP_OTHER modifier is present
		// then read another event without advancing the script.
		debug("Event (%s) does not match expected (%s) -- skipping",
		      event_type_names[ep->type], event_type_names[scrp->expect]);
		continue;
	      } else {
		// Otherwise, failure to match is an error.
		fprintf(stderr, "Read event type %s, expected type %s\n",
			event_type_names[ep->type], event_type_names[scrp->expect]);
		goto abort_test;
	      }
	    }
	    if(ep == NULL)
		goto abort_test;
	}
	scrp++;
    }
    // Wait for child and get exit status.
    if(cmd_str != NULL) {
	fclose(cmd_str);
	cmd_str = NULL;
    }
    if(pid && waitpid(-1, statusp, 0) < 0) {
	perror("waitpid");
	return -1;
    }
    fprintf(stderr, "End of test %s\n", name);
    return 0;

 abort_test:
    fprintf(stderr, "Aborting test %s\n", name);
    if(pid) {
	killpg(pid, SIGKILL);
	killpg(pid, SIGCONT);
    }
    return -1;
}

static int setup_socket(char *name) {
    int listen_fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    if(listen_fd < 0) {
	perror("socket");
	exit(1);
    }
    struct sockaddr_un sa;
    sa.sun_family = AF_LOCAL;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", name);
    unlink((char *)sa.sun_path);
    if(bind(listen_fd, (struct sockaddr *)&sa, sizeof(struct sockaddr_un)) < 0) {
	perror("bind");
	exit(1);
    }
    if(listen(listen_fd, 0) < 0) {
	perror("listen");
	exit(1);
    }
    return listen_fd;
}

static FILE *run_target(char *prog, char *av[], int *pidp) {
    FILE *cmdstr;
    int cmd_pipe[2];
    int pid;

    if(pipe(cmd_pipe) == -1) {
	perror("pipe");
	exit(1);
    }
    if((pid = fork()) == 0) {
        struct rlimit rl;
	rl.rlim_cur = rl.rlim_max = 60;  // 60 seconds CPU maximum
	setrlimit(RLIMIT_CPU, &rl);
	rl.rlim_cur = rl.rlim_max = 1000;  // 1000 threads maximum
	setrlimit(RLIMIT_NPROC, &rl);
	// Create a new process group so that we can nuke wayward children when
	// we are done.
	setpgid(0, 0);
        signal(SIGPIPE, SIG_DFL);
	
	dup2(cmd_pipe[0], 0);
	close(cmd_pipe[0]);
	close(cmd_pipe[1]);
	if(execvp(av[0], av) == -1) {
	    perror("exec");
	    exit(1);
	}
    }
    close(cmd_pipe[0]);
    if((cmdstr = fdopen(cmd_pipe[1], "w")) == NULL) {
	perror("fdopen");
	exit(1);
    }
    if(pidp)
	*pidp = pid;
    return cmdstr;
}

static struct timeval current_timeout;

static void alarm_handler(int sig) {
    struct timeval now;
    gettimeofday(&now, NULL);
    fprintf(stderr, "%ld.%06ld: Driver: timeout (%ld, %ld)\n",
	    now.tv_sec, now.tv_usec, current_timeout.tv_sec, current_timeout.tv_usec);
}

static EVENT *read_event(int fd, struct timeval *timeout) {
    EVENT event, *ep;
    int n;
    struct itimerval itv = {0};
    struct sigaction sa = {0}, oa;
    sa.sa_handler = alarm_handler;
    // No SA_RESTART
    itv.it_value = *timeout;
    current_timeout = *timeout;
    sigaction(SIGALRM, &sa, &oa);
    setitimer(ITIMER_REAL, &itv, NULL);
    memset(&itv, 0, sizeof(itv));
    if((n = read(fd, &event, sizeof(event))) != sizeof(event)) {
	setitimer(ITIMER_REAL, &itv, NULL);  // Cancel timer
	if(n != 0) {
	    if(errno == EINTR)
		fprintf(stderr, "Timeout reading event from child\n");
	    else
		perror("Error reading event");
	} else {
	    fprintf(stderr, "Short count reading event\n");
	}
	return NULL;
    }
    setitimer(ITIMER_REAL, &itv, NULL);  // Cancel timer
    sigaction(SIGALRM, &oa, NULL);
    struct timeval now;
    gettimeofday(&now, NULL);
    fprintf(stderr, "%ld.%06ld: Driver <~~ %s (sent at %ld.%06ld)\n",
	    now.tv_sec, now.tv_usec, event_type_names[event.type],
	    event.time.tv_sec, event.time.tv_usec);
    ep = malloc(sizeof(event));
    memcpy(ep, &event, sizeof(event));
    return(ep);
}
