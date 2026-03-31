#include <criterion/criterion.h>
#include <criterion/logging.h>

#include "legion.h"
#include "event.h"
#include "tracker.h"
#include "__helper.h"

static char *daemon_status_names[] = {
    [status_unknown] "unknown",
    [status_inactive] "inactive",
    [status_starting] "starting",
    [status_active] "active",
    [status_stopping] "stopping",
    [status_exited] "exited",
    [status_crashed] "crashed"
};

#define LOG_DIR "logs"
#define LEGION_EXECUTABLE "bin/legion"

extern char *daemon_status_names[];


void assert_proper_exit_status(int err, int status) {
    cr_assert_eq(err, 0, "The test driver returned an error (%d)\n", err);
    cr_assert_eq(status, 0, "The program did not exit normally (status = 0x%x)\n", status);
}

void assert_daemon_state_change(EVENT *ep, int *env, void *args) {
    long state = (long)args;
    DAEMON *daemon = find_daemon(ep->name);
    cr_assert(daemon != NULL, "Daemon (name '%s' pid %d) does not exist",
              ep->name, ep->pid);
    cr_assert_eq(daemon_state(daemon), state,
        "Daemon '%s' state change is not the expected (Got: %s, Expected: %s)",
        ep->name, daemon_status_names[state], daemon_status_names[state]);
}

void assert_num_daemons(EVENT *ep, int *env, void *args) {
    long num = (long)args;
    cr_assert_eq(num, num_daemons,
		 "Number of daemons is not expected (Got: %d, Expected: %ld)\n", num_daemons, num);
}

void assert_log_exists_check(char *logfile_name, int version_indicator) {
    char full_logfile_name[500];
    sprintf(full_logfile_name,"%s/%s.log.%d", LOG_DIR, logfile_name, version_indicator);

    //Check if file exists
    int fileExists = access(full_logfile_name, F_OK);
    cr_assert_neq(fileExists, -1,
        "File '%s' does not exist \n", full_logfile_name);

    FILE *file = fopen(full_logfile_name, "r");
    if (!file)
        env_error_abort_test("Error opening file");

    //Reads one line of the log file (Content existence check)
    char *content_buf = NULL;
    size_t content_size = 0;
    ssize_t ret_val = getline(&content_buf, &content_size, file);

    free(content_buf);
    content_buf = NULL;
    fclose(file);
    cr_assert_neq(ret_val, -1, "File '%s' is invalid\n", full_logfile_name);
}

void env_error_abort_test(char* msg) {
    fprintf(stderr, "%s", msg);
    cr_skip_test();
}


void get_formatted_cmd_input(char *args[], int len, char *final_cmd) {

    strcpy(final_cmd, "(");
    for(int i = 0; i < len; i++)
    {
        char formatted_cmd[50] = {0};
        char *cmd = args[i];
        sprintf(formatted_cmd,"echo %s%s", cmd, i < len-1 ? "; " : ") | ");
        strcat(final_cmd, formatted_cmd);
    }
    strcat(final_cmd, LEGION_EXECUTABLE);
}

void assert_diff_check(char *full_answer_logfile_path, char *logfile_name, int version_indicator)
{
    char full_logfile_path[100];
    sprintf(full_logfile_path,"%s/%s.log.%d", LOG_DIR, logfile_name, version_indicator);

    char cmd[500];
    sprintf(cmd,
        "diff --ignore-tab-expansion --ignore-trailing-space "
        "--ignore-space-change --ignore-blank-lines %s %s",
        full_answer_logfile_path, full_logfile_path);
    int err = system(cmd);
    cr_assert_eq(err, 0,
       "The output was not what was expected (diff exited with "
       "status %d).\n",
       WEXITSTATUS(err));
}
