
#define EVENT_SOCKET "event_socket"

#define SERVICE_NAME_MAX 32
#define COMMAND_MAX 32

typedef enum event_type {
    NO_EVENT, ANY_EVENT, EOF_EVENT,
    INIT_EVENT, FINI_EVENT,
    REGISTER_EVENT, UNREGISTER_EVENT,
    START_EVENT, ACTIVE_EVENT, STOP_EVENT, KILL_EVENT, RESET_EVENT,
    LOGROTATE_EVENT, TERM_EVENT, CRASH_EVENT, ERROR_EVENT,
    PROMPT_EVENT, STATUS_EVENT
} EVENT_TYPE;

typedef struct event {
    int type;
    int pid;
    int exit_status;
    int signal;
    int oldstate;
    int newstate;
    char name[SERVICE_NAME_MAX];
    char command[COMMAND_MAX];
    struct timeval time;
} EVENT;

//extern char *event_type_names[];
