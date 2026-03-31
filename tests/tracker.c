#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/time.h>
#include <sys/types.h>

#include "legion.h"
#include "event.h"
#include "tracker.h"

typedef struct daemon {
    char name[SERVICE_NAME_MAX];        /* Name of the daemon */
    int pid;                            /* Process ID of worker. */
    int state;                          /* Worker state. */
    int exited;                         /* Has worker exited? */
    union {
        int exit_status;
        int crash_signal;
    };
    struct timeval last_change_time;    /* Time of last state change for the worker. */
    struct timeval last_event_time;     /* Time of last event for the worker. */
    struct timeval next_event_timeout;  /* Timeout for next event (0 for no timeout). */
} DAEMON;

int daemon_state(DAEMON *daemon) { return daemon->state; }

DAEMON daemon_table[MAX_DAEMONS];
int num_daemons = 0;

/* Defines valid worker state changes */
#define DAEMON_STATE_MAX (status_crashed+1)
/*
enum daemon_status {
    status_unknown, status_inactive, status_starting, status_active,
    status_stopping, status_exited, status_crashed
};
register: unknown -> inactive
unregister: inactive -> unknown
start: inactive -> starting, starting -> active
stop: exit -> inactive, crashed -> inactive, active -> stopping, stopping -> exited, stopping -> crashed
logrotate: active -> (stopping...exited...starting..) -> active
(Daemon stops itself): active -> exited, active -> crashed
*/
static char *daemon_state_names[] = {
    [status_unknown] = "unknown",
    [status_inactive] = "inactive",
    [status_starting] = "starting",
    [status_active] = "active",
    [status_stopping] = "stopping",
    [status_exited] = "exited",
    [status_crashed] = "crashed"
};

static int valid_transitions[DAEMON_STATE_MAX][DAEMON_STATE_MAX] = {
    /*                    unknown, inactive,starting,active, stopping, exited,    crashed */
    /* unknown */       { 0,       0,       0,       0,      0,        0,         0},
    /* inactive */      { 1,       1,       1,       0,      0,        0,         0},
    /* starting */      { 0,       1,       0,       1,      1,        0,         0},
    /* active. */       { 0,       0,       0,       1,      1,        1,         1},
    /* stopping */      { 0,       0,       0,       0,      1,        1,         1},
    /* exited */        { 0,       1,       1,       0,      0,        0,         0},
    /* crashed */       { 0,       1,       0,       0,      0,        0,         0}
};

/*
 * Predefined timeout values.
 */
#define ZERO_SEC { 0, 0 }
#define ONE_USEC { 0, 1 }
#define ONE_MSEC { 0, 1000 }
#define TEN_MSEC { 0, 10000 }
#define HND_MSEC { 0, 100000 }
#define ONE_SEC  { 1, 0 }

static struct timeval transition_timeouts[DAEMON_STATE_MAX][DAEMON_STATE_MAX] = {
    /*                    unknown,   inactive,  starting,  active,    stopping,  exited,    crashed */
    /* unknown */       { ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC},
    /* inactive */      { ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC},
    /* starting */      { ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  HND_MSEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC},
    /* active */        { ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC},
    /* stopping */      { ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  HND_MSEC,  ZERO_SEC},
    /* exited */        { ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC},
    /* crashed */       { ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC,  ZERO_SEC}
};

static int master_started = 0;
static int master_has_error = 0;

static void show_daemon(DAEMON *daemon) {
    fprintf(stderr, "[%d: %s, %d]",
            daemon->pid, daemon_state_names[daemon->state], daemon->exited);
}

void tracker_show_daemon(void) {
    fprintf(stderr, "DAEMONS: ");
    for(int i = 0; i < MAX_DAEMONS; i++) {
        DAEMON *daemon = &daemon_table[i];
        if(daemon->pid) {
            show_daemon(daemon);
        }
    }
    fprintf(stderr, "\n");
}

/**
 * Checks for a valid transition, within any specified time limit.
 * @param daemon the daemon changing state
 * @param old the old status
 * @param new the new status
 * @return nonzero if transition is valid, 0 otherwise
 */
static int valid_transition(DAEMON *daemon, int old, int new, struct timeval *when) {
    if(old < 0 || old >= DAEMON_STATE_MAX || new < 0 || new >= DAEMON_STATE_MAX)
        return 0;
    if(!valid_transitions[old][new]) {
        fprintf(stderr, "Daemon %d transition is invalid (old %s, new %s)\n",
                daemon->pid, daemon_state_names[old], daemon_state_names[new]);
        return 0;
    }
    struct timeval timeout = transition_timeouts[old][new];
    if(timeout.tv_sec == 0 && timeout.tv_usec == 0)
        return 1;
    struct timeval exp_time = daemon->last_change_time;
    exp_time.tv_usec += timeout.tv_usec;
    while(exp_time.tv_usec >= 1000000) {
        exp_time.tv_sec++;
        exp_time.tv_usec -= 1000000;
    }
    exp_time.tv_sec += timeout.tv_sec;
    if(when->tv_sec > exp_time.tv_sec ||
       (when->tv_sec == exp_time.tv_sec && when->tv_usec > exp_time.tv_usec)) {
        fprintf(stderr, "Daemon %d transition (old %s, new %s) is too late "
                "(exp = %ld.%06ld, sent = %ld.%06ld)\n",
                daemon->pid, daemon_state_names[old], daemon_state_names[new],
                exp_time.tv_sec, exp_time.tv_usec, when->tv_sec, when->tv_usec);
        return 0;
    }
#if 0
    fprintf(stderr, "Daemon %d transition (old %s, new %s) is timely "
            "(exp = %ld.%06ld, sent = %ld.%06ld)\n",
            daemon->pid, daemon_state_names[old], daemon_state_names[new],
            exp_time.tv_sec, exp_time.tv_usec, when->tv_sec, when->tv_usec);
#endif
    return 1;
}

/**
 * Finds the index of the daemon in the worker table.
 * @param pid The pid of the daemon to find.
 * @return the worker corresponding to pid, NULL else.
 */
DAEMON *find_daemon(const char *name) {
    for(int i = 0; i < MAX_DAEMONS; i++) {
        if(strncmp(name, daemon_table[i].name, SERVICE_NAME_MAX) == 0)
            return &daemon_table[i];
    }
    return NULL;
}

int time_increases(struct timeval *tv1, struct timeval *tv2) {
    return (tv1->tv_sec < tv2->tv_sec ||
            (tv1->tv_sec == tv2->tv_sec && tv1->tv_usec < tv2->tv_usec));
}

static int add_daemon(const char *dname, EVENT *ep)
{
    if (num_daemons >= MAX_DAEMONS) {
        fprintf(stderr, "Cannot add more daemons: reached maximum (%d)\n",
                MAX_DAEMONS);
        return -1;
    }
    DAEMON *newd = &daemon_table[num_daemons];
    newd->pid = 0;
    strncpy(newd->name, dname, SERVICE_NAME_MAX);
    newd->state = status_inactive;
    newd->exited = 0;
    newd->last_change_time = ep->time;
    newd->last_event_time = ep->time;
    newd->next_event_timeout = transition_timeouts[status_inactive][status_starting];
    num_daemons++;
    return 0;
}

static int remove_daemon(const char *dname)
{
    int pos = -1;
    for (int i = 0; i < num_daemons; ++i) {
        if (strncmp(daemon_table[i].name, dname, SERVICE_NAME_MAX) == 0) {
            pos = i;
            break;
        }
    }
    /* Only inactive daemons can be unregistered. Otherwise, it's an error */
    if (daemon_table[pos].state != status_inactive) {
        fprintf(stderr, "Daemon '%s' is not in active state and thus cannot be "
                "unregistered.", dname);
        return -1;
    }
    if (pos == -1) {
        fprintf(stderr, "Daemon of name '%s' is not found, couldn't delete\n",
                dname);
        return -1;
    }
    for (int j = pos + 1; j < num_daemons; ++j) {
        daemon_table[j - 1] = daemon_table[j];
    }
    memset(&daemon_table[num_daemons], 0, sizeof(DAEMON));
    num_daemons--;
    return 0;
}

/* Check if the tracker has received any ERROR_EVENT.
 * NOTE: This function is NOT idempotent and will clear
 * the error state once called.
 */
bool legion_has_error()
{
    bool res = (master_has_error != 0);
    master_has_error = 0;
    return res;
}

int track_event(EVENT *ep) {
    if (!master_started) {
        if (ep->type == INIT_EVENT)
            master_started = 1;
        else
            fprintf(stderr, "First event is not INIT.\n");
        return 0;
    }
    DAEMON *daemon = find_daemon(ep->name);

    switch(ep->type) {
    case INIT_EVENT:
        fprintf(stderr, "Redundant INIT_EVENT received.\n");
        return -1;

    case REGISTER_EVENT:
        if (!daemon) {
            return add_daemon(ep->name, ep);
        } else {
            fprintf(stderr, "Duplicate registration of the same daemon.\n");
            return -1;
        }

    case UNREGISTER_EVENT:
        if (!daemon) {
            fprintf(stderr, "Cannot unregister an nonexistent daemon '%s'\n",
                    ep->name);
            return -1;
        }
        return remove_daemon(daemon->name);

    case START_EVENT:
        if (!daemon) {
            fprintf(stderr, "Cannot start an nonexistent daemon '%s'\n",
                    ep->name);
            return -1;
        }
        if (!valid_transition(daemon, daemon->state, status_starting, &ep->time)) {
            return -1;
        }
        daemon->state = status_starting;
        return 0;

    case ACTIVE_EVENT:
        if (!daemon) {
            fprintf(stderr, "Cannot activate an nonexistent daemon '%s'\n",
                    ep->name);
            return -1;
        }
        if (daemon->state != status_starting) {
            fprintf(stderr, "active_event: the daemon '%s' is supposed to be "
                    "in 'starting' state, but got '%s' state.\n", daemon->name,
                    daemon_state_names[daemon->state]);
            return -1;
        }
        daemon->state = status_active;
        daemon->exited = 0;
        daemon->pid = ep->pid;
        return 0;

    case STOP_EVENT:
        if (!daemon) {
            fprintf(stderr, "Cannot stop an nonexistent daemon '%s'\n",
                    ep->name);
            return -1;
        }
        /* stop command on daemons with the exited or crashed state should
         * belong to "reset" event */
        if (daemon->state != status_active) {
            fprintf(stderr, "stop_event: the daemon '%s' should be in 'active' "
                    "state, but got '%s' state.\n", daemon->name,
                    daemon_state_names[daemon->state]);
            return -1;
        }
        if (daemon->pid != ep->pid) {
            fprintf(stderr, "stop_event: daemon '%s' has pid %d, but the event "
                    "says pid = %d, which is a mismatch.\n", daemon->name,
                    daemon->pid, ep->pid);
            return -1;
        }
        daemon->state = status_stopping;
        return 0;

    case KILL_EVENT:
        if (!daemon) {
            fprintf(stderr, "Cannot kill an nonexistent daemon '%s'\n",
                    ep->name);
            return -1;
        }
	// before active, daemon->pid is unassigned
        if (daemon->pid!=0 && daemon->pid != ep->pid) {
            fprintf(stderr, "kill_event: daemon '%s' has pid %d, but the event "
                    "says pid = %d, which is a mismatch.\n", daemon->name,
                    daemon->pid, ep->pid);
            return -1;
        }
        daemon->state = status_stopping;
        return 0;

    case RESET_EVENT:
        if (!daemon) {
            fprintf(stderr, "Cannot reset an nonexistent daemon '%s'\n",
                    ep->name);
            return -1;
        }
        if (!valid_transition(daemon, daemon->state, status_inactive, &ep->time)) {
            return -1;
        }
        if (daemon->state == status_unknown) {
            fprintf(stderr, "reset_event: the daemon '%s' has 'unknown' state. "
                    "This is very wrong!\n", daemon->name);
            return -1;
        }
        daemon->state = status_inactive;
        return 0;

    case LOGROTATE_EVENT:
	/* If the daemon is running, the logrotate operation should stop the
	 * daemon, rotate the log and start the daemon again. There should be
	 * STOP, TERM, START, ACTIVE events following this, and there is nothing
	 * to do here.
	 */
	return 0;

    case TERM_EVENT:
        if (!daemon) {
            fprintf(stderr, "Cannot terminate daemon '%s' that doesn't exist\n",
                    ep->name);
            return -1;
        }
        if (!valid_transition(daemon, daemon->state, status_exited, &ep->time)) {
            return -1;
        }
        if (daemon->pid != ep->pid) {
            fprintf(stderr, "term_event: daemon '%s' has pid %d, but the event "
                    "says pid = %d, which is a mismatch.\n", daemon->name,
                    daemon->pid, ep->pid);
            return -1;
        }
        daemon->state = status_exited;
        daemon->exited = 1;
        daemon->exit_status = ep->exit_status;
        return 0;

    case CRASH_EVENT:
        if (!daemon) {
            fprintf(stderr, "Cannot crash the daemon '%s' that doesn't exist\n",
                    ep->name);
            return -1;
        }
        if (!valid_transition(daemon, daemon->state, status_crashed, &ep->time)) {
            return -1;
        }
	// when daemon->pid==0 and it crashes, it means that it crashed after start but before active
        if (daemon->pid!=0 && daemon->pid != ep->pid) {
            fprintf(stderr, "crash_event: daemon '%s' has pid %d, but the event "
                    "says pid = %d, which is a mismatch.\n", daemon->name,
                    daemon->pid, ep->pid);
            return -1;
        }
        daemon->state = status_crashed;
        daemon->exited = 1;
        daemon->crash_signal = ep->signal;
        return 0;

    case ERROR_EVENT:
        master_has_error = 1;
        return 0;

    case PROMPT_EVENT:
	// Ignore for now.
	return 0;

    case STATUS_EVENT:
	// Ignore for now.
	return 0;

    case FINI_EVENT:
        for (int i = 0; i < MAX_DAEMONS; i++) {
            DAEMON *d = daemon_table + i;
            if (d->pid)
                if (!d->exited) {
                    fprintf(stderr, "The legion is ending while daemon with "
                            "name %s pid %d is not exited yet.\n",
                            d->name, d->pid);
                    return -1;
                }
        }
        master_started = 0;
        break;
    default:
        fprintf(stderr, "Attempt to track unrecognized event type %d\n", ep->type);
        return -1;
    }
    return 0;
}
