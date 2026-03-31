#include <stdio.h>

#include <criterion/criterion.h>
#include <criterion/logging.h>

#include "event.h"
#include "driver.h"
#include "tracker.h"
#include "__helper.h"

#define QUOTE1(x) #x
#define QUOTE(x) QUOTE1(x)
#define SCRIPT1(x) x##_script
#define SCRIPT(x) SCRIPT1(x)

#define LEGION_EXECUTABLE "bin/legion"
#define VALGRIND "valgrind --error-exitcode=37 --child-silent-after-fork=yes"

/*
 * Finalization function to try to ensure no stray processes are left over
 * after a test finishes.
 */
static void killall() {
  char *progs_to_kill[] = {
      "bin/legion",
      "crash",
      "lazy", 
      "systat",
      "logcheck",
      "nosync",
      NULL
  };
  char cmd[64];
  for(char **pp = progs_to_kill; *pp; pp++) {
      snprintf(cmd, sizeof(cmd), "killall -s KILL %s > /dev/null 2>&1", *pp);
      system(cmd);
  }
}

/*
 * The following just tests that your command-line interface does not loop
 * when it gets an end-of-file on the standard input.  This will be important
 * when during our grading tests, which will use a driver program to interact
 * with yours, rather than entering commands manually.
 */

/*---------------------------test eof------------------------------------*/
#define SUITE basecode_suite
#define TEST_NAME eof_test
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,            expect,			modifiers,  timeout,    before,    after
    {  NULL,		INIT_EVENT,		0,          HND_MSEC,   NULL,      NULL },
    {  NULL,		PROMPT_EVENT,		0,          HND_MSEC,   NULL,      NULL },
    {  SEND_EOT,	ERROR_EVENT,		0,          HND_MSEC,	NULL,      NULL },
    {  NULL,            FINI_EVENT,		0,          TWO_SEC,	NULL,      NULL },
    {  NULL,            EOF_EVENT,		0,          TEN_MSEC,   NULL,      NULL }
};


Test(SUITE, TEST_NAME, .fini = killall, .timeout = 30)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {LEGION_EXECUTABLE, "auto", NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef TEST_NAME

/*
 * The following tests that the quit command works properly..
 */

/*---------------------------test quit------------------------------------*/
#define SUITE basecode_suite
#define TEST_NAME quit_test
#define quit_cmd "quit"
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,            expect,			modifiers,  timeout,    before,    after
    {  NULL,		INIT_EVENT,		0,          HND_MSEC,   NULL,      NULL },
    {  NULL,		PROMPT_EVENT,		0,          HND_MSEC,   NULL,      NULL },
    {  quit_cmd,	NO_EVENT,		0,          HND_MSEC,	NULL,      NULL },
    {  NULL,		FINI_EVENT,		0,          TWO_SEC,	NULL,      NULL },
    {  NULL,            EOF_EVENT,		0,          TEN_MSEC,   NULL,      NULL }
};


Test(SUITE, TEST_NAME, .fini = killall, .timeout = 30)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {LEGION_EXECUTABLE, "auto", NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef quit_cmd
#undef TEST_NAME

/*
 * The following tests that the status-all command produces no output
 * if nothing is registered.
 */

/*---------------------------test status-all empty------------------------------------*/
#define SUITE basecode_suite
#define TEST_NAME status_all_empty_test
#define status_all_cmd "status-all"
#define quit_cmd "quit"
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,            expect,			modifiers,  timeout,    before,    after
    {  NULL,		INIT_EVENT,		0,          HND_MSEC,   NULL,      NULL },
    {  NULL,		PROMPT_EVENT,		0,          HND_MSEC,   NULL,      NULL },
    {  status_all_cmd,	PROMPT_EVENT,		0,          HND_MSEC,	NULL,      NULL },
    {  quit_cmd,	NO_EVENT,		0,          HND_MSEC,	NULL,      NULL },
    {  NULL,		FINI_EVENT,		0,          TWO_SEC,	NULL,      NULL },
    {  NULL,            EOF_EVENT,		0,          TEN_MSEC,   NULL,      NULL }
};


Test(SUITE, TEST_NAME, .fini = killall, .timeout = 30)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {LEGION_EXECUTABLE, "auto", NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef status_all_cmd
#undef quit_cmd
#undef TEST_NAME

/*
 * The following tests that a daemon can be registered, after which the status-all
 * command produces a single output line.  The content is not currently checked.
 */

/*---------------------------test status-all single------------------------------------*/
#define SUITE basecode_suite
#define TEST_NAME status_all_single_test
#define register_cmd "register lazy lazy"
#define status_all_cmd "status-all"
#define quit_cmd "quit"
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,            expect,			modifiers,  timeout,    before,    after
    {  NULL,		INIT_EVENT,		0,          HND_MSEC,   NULL,      NULL },
    {  NULL,		PROMPT_EVENT,		0,          HND_MSEC,   NULL,      NULL },
    {  register_cmd,	REGISTER_EVENT,		0,          HND_MSEC,	NULL,      NULL },
    {  NULL,		PROMPT_EVENT,		0,          HND_MSEC,   NULL,      NULL },
    {  status_all_cmd,	STATUS_EVENT,		0,          HND_MSEC,	NULL,      NULL },
    {  NULL,		PROMPT_EVENT,		0,          HND_MSEC,   NULL,      NULL },
    {  quit_cmd,	NO_EVENT,		0,          HND_MSEC,	NULL,      NULL },
    {  NULL,		FINI_EVENT,		0,          TWO_SEC,	NULL,      NULL },
    {  NULL,            EOF_EVENT,		0,          TEN_MSEC,   NULL,      NULL }
};


Test(SUITE, TEST_NAME, .fini = killall, .timeout = 30)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {LEGION_EXECUTABLE, "auto", NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef register_cmd
#undef status_all_cmd
#undef quit_cmd
#undef TEST_NAME


/*
 * This test extends the previous one by trying to start the daemon that was registered.
 */

/*---------------------------test start single ------------------------------------*/
#define SUITE basecode_suite
#define TEST_NAME start_single_test
#define register_cmd "register lazy lazy"
#define start_cmd "start lazy"
#define quit_cmd "quit"
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,            expect,			modifiers,  timeout,    before,    after
    {  NULL,		INIT_EVENT,		0,          HND_MSEC,   NULL,      NULL },
    {  NULL,		PROMPT_EVENT,		0,          HND_MSEC,   NULL,      NULL },
    {  register_cmd,	REGISTER_EVENT,		0,          HND_MSEC,	NULL,      NULL },
    {  NULL,		PROMPT_EVENT,		0,          HND_MSEC,   NULL,      NULL },
    {  start_cmd,	START_EVENT,		0,          HND_MSEC,	NULL,      NULL },
    {  NULL,		ACTIVE_EVENT,		0,          HND_MSEC,   NULL,      NULL },
    {  NULL,		PROMPT_EVENT,		0,          HND_MSEC,   NULL,      NULL },
    {  quit_cmd,	NO_EVENT,		0,          HND_MSEC,	NULL,      NULL },
    {  NULL,		STOP_EVENT,		0,          HND_MSEC,   NULL,      NULL }, 
    {  NULL,		TERM_EVENT,		0,          HND_MSEC,   NULL,      NULL }, 
    {  NULL,		FINI_EVENT,		0,          TWO_SEC,	NULL,      NULL },
    {  NULL,            EOF_EVENT,		0,          TEN_MSEC,   NULL,      NULL }
};


Test(SUITE, TEST_NAME, .fini = killall, .timeout = 30)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {LEGION_EXECUTABLE, "auto", NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef register_cmd
#undef start_cmd
#undef quit_cmd
#undef TEST_NAME

