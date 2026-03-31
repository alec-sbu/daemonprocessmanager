#ifndef __HELPER_H
#define __HELPER_H

void assert_proper_exit_status(int err, int status);
void assert_daemon_status_change(EVENT *ep, int *env, void *args);
void assert_num_daemons(EVENT *ep, int *env, void *args);
void assert_log_exists_check(char *logfile_name, int version_indicator);
void env_error_abort_test(char* msg);
void get_formatted_cmd_input(char **args, int len, char *final_cmd);
void assert_diff_check(char *full_answer_logfile_path, char *logfile_name, int version_indicator);

#endif
