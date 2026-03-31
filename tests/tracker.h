#define MAX_DAEMONS   100

typedef struct daemon DAEMON;

extern int num_daemons;

DAEMON *find_daemon(const char *name);
int daemon_state(DAEMON *worker);
int track_event(EVENT *ep);
bool legion_has_error();
