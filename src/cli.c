/*
 * Legion: Command-line interface
 */

#include "legion.h"
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <errno.h>
extern char **environ;
pid_t childPid;
int globalInFd;
const char *quit = "quit";
// 0 means signal successfully sent, -1 error, 1 for no signal sent
volatile sig_atomic_t processKilled=1;
volatile sig_atomic_t sigintReceived=0;
volatile sig_atomic_t sigchldReceived=0;
void sigchld_handler(int signum){
    sigchldReceived = 1;
}
void sigint_handler(int signum){
    sigintReceived = 1;
}
void sigalrm_handler(int signum){
    processKilled = kill(childPid,SIGKILL);
}
const char* status[] = {
        "status_unknown",
        "status_inactive",
        "status_starting",
        "status_active",
        "status_stopping",
        "status_exited",
        "status_crashed"
    };
typedef struct daem {
    int pid;
    int state;
    union {
        int exit_status;
        int crash_signal;
    };
    int numArgs;
    char *args[1024];
} daem;
// return daemon ptr given a name, daem array, and numDaemons
// return NULL if daemon with name is not found
daem* returnDaem(char* name, daem* daemons[],int numDaemons){
    for(int i = 0; i<numDaemons; i++){
        if(strcmp(name,daemons[i]->args[1])==0){
            return daemons[i];
        }
    }
    return NULL;
}
// return index of daemon given name, daem array, and numDaemons
// return -1 if there is no index with name
int returnDaemIndex(char* name, daem* daemons[],int numDaemons){
    for(int i = 0; i<numDaemons; i++){
        if(strcmp(name,daemons[i]->args[1])==0){
            return i;
        }
    }
    return -1;
}
// creates path to logfile with corresponding name and num when given a pointer to path string
void createNewLogPath(char* logPath, char* name, char* num){
    logPath[0] = '\0';
    strcat(logPath,LOGFILE_DIR);
    strcat(logPath,"/");
    strcat(logPath,name);
    strcat(logPath,".log.");
    strcat(logPath,num);
}
// check if logs directory exists, if not then it creates it
// return 0 on found, -1 on unable to create
int checkLogDir(){
    DIR *logDir = opendir(LOGFILE_DIR);
    if(logDir){
        closedir(logDir);
    }
    else{
        if((mkdir(LOGFILE_DIR,0777)!=0)){
            sf_error("Unable to creact new log directory");
            return -1;
        }
    }
    return 0;
}
// must free all of the args malloced and then free the daemon itself
void free_daemon(daem* daemon){
    for(int i = 0; i<daemon->numArgs; i++){
        free(daemon->args[i]);
    }
    free(daemon);
}
// unregisters the daemon at index of array, fills in the gap if necessary
void unregister_daemon(daem* daemons[], int index, int* numDaemons){
    free_daemon(daemons[index]);
    for(int i = index+1; i<*numDaemons; i++){
        daemons[i-1] = daemons[i];
    }
    *numDaemons = *numDaemons-1;
}
void print_status(daem* dPtr, FILE *out){
    char msg[1024];
    char num[1024];
    num[0] = '\0';
    msg[0] = '\0';
    strcat(msg,dPtr->args[1]);
    strcat(msg,"\t");
    sprintf(num,"%d",dPtr->pid);
    strcat(msg,num);
    num[0] = '\0';
    strcat(msg,"\t");
    strcat(msg,status[dPtr->state]);
    sf_status(msg);
    fprintf(out, "%s\n",msg);
    fflush(out);
}

// start new daemon given pointer to daemon
// return 0 on success -1 on error, 1 if sync byte not received in time
int startDaemon(daem* dPtr){
    sf_start(dPtr->args[1]);
    dPtr->state = status_starting;
    int fd[2], pid;
    if(pipe(fd)<0){
        sf_error("Error creating pipe");
        return -1;
    }
    pid = fork();
    if(pid==-1){
        sf_error("Error forking child process");
        dPtr->state = status_inactive;
        return -1;
    }
    if(pid==0){ //child
        // redirect SYNC_FD to pipe input so parent can see sync byte
        if(dup2(fd[1],SYNC_FD)==-1){
            sf_error("Error redirecting to SYNC_FD");
            return -1;
        }
        // redirect stdout to appropriate log file
        char logPath[1024];
        createNewLogPath(logPath,dPtr->args[1],"0");
        if(checkLogDir()==-1){
            return -1;
        }
        FILE *log = freopen(logPath,"a",stdout);
        if(log==NULL){
            sf_error("Error opening log");
            return -1;
        }
        // join unique process group
        if(setpgid(0,0)==-1){
            sf_error("Error setting pgid");
            return -1;
        }
        // execute the process
        // first prepare environ variable
        char* originalPath = getenv("PATH");
        if(originalPath == NULL){
            sf_error("Error getting path");
            return -1;
        }
        char* newPath = malloc((strlen(DAEMONS_DIR)+1+strlen(originalPath)+1)*sizeof(char));
        if(newPath==NULL){
            sf_error("Error mallocing path");
            return -1;
        }
        newPath[0] = '\0';
        strcat(newPath,DAEMONS_DIR);
        strcat(newPath,":");
        strcat(newPath,originalPath);
        if(setenv("PATH",newPath,1)==-1){
            sf_error("Error setting path to new environ");
            return -1;
        }
        free(newPath);
        // new environment prepared now execvpe the daemon
        if(execvpe(dPtr->args[2],(&(dPtr->args[2])),environ)==-1){
            sf_error("Error executing daemon");
            return -1;
        }
    }
    else{ // parent
        // make parent block SIGCHLD until state is set to active
        sigset_t SIGCHLDMASK;
        sigemptyset(&SIGCHLDMASK);
        sigaddset(&SIGCHLDMASK,SIGCHLD);
        sigset_t emptyMask;
        sigemptyset(&emptyMask);
        if(sigprocmask(SIG_BLOCK,&SIGCHLDMASK,NULL)==-1){
            sf_error("Error masking SIGCHLD before forking");
            return -1;
        }
        dPtr->pid = pid;
        ssize_t ret;
        char syncByte;
        // close write fd
        if(close(fd[1])<0){
            sf_error("Error closing write file descriptor");
            return -1;
        }
        //read the sync byte
        processKilled = 1;
        childPid = dPtr->pid;
        alarm(CHILD_TIMEOUT);
        ret = read(fd[0],&syncByte,sizeof(char));
        alarm(0);
        // close read fd
        // if a sigkill was sent wait for sigchld before proceeding
        if(processKilled != 1){
            sigsuspend(&emptyMask);
        }
        if(close(fd[0])<0){
            sf_error("Error closing read file descriptor");
            if(sigprocmask(SIG_UNBLOCK,&SIGCHLDMASK,NULL)==-1){
                sf_error("Error unmasking SIGCHLD");
                return -1;
            }
            return -1;
        }
        if(processKilled == -1){
            sf_error("Unable to kill child process");
            if(sigprocmask(SIG_UNBLOCK,&SIGCHLDMASK,NULL)==-1){
                sf_error("Error unmasking SIGCHLD");
                return -1;
            }
            return -1;
        }
        if(processKilled == 0){
            sf_error("Sync byte not received in time");
            if(sigprocmask(SIG_UNBLOCK,&SIGCHLDMASK,NULL)==-1){
                sf_error("Error unmasking SIGCHLD");
                return -1;
            }
            return 1;
        }
        if(ret == -1){
            sf_error("Error reading sync byte");
            if(sigprocmask(SIG_UNBLOCK,&SIGCHLDMASK,NULL)==-1){
                sf_error("Error unmasking SIGCHLD");
                return -1;
            }
            return -1;
        }
        else if(ret == 0){
            sf_error("Reading sync byte returned 0");
            if(sigprocmask(SIG_UNBLOCK,&SIGCHLDMASK,NULL)==-1){
                sf_error("Error unmasking SIGCHLD");
                return -1;
            }
            return -1;
        }
        else{
            dPtr->state = status_active;
            sf_active(dPtr->args[1],pid);
            if(sigprocmask(SIG_UNBLOCK,&SIGCHLDMASK,NULL)==-1){
                sf_error("Error unmasking SIGCHLD");
                return -1;
            }
        }
    }
    return 0;
}
// return 0 on success, -1 on error
int stopDaemon(daem* daemPtr,FILE* out){
    if(daemPtr->state==status_exited || daemPtr->state == status_crashed){
        daemPtr->state = status_inactive;
        sf_reset(daemPtr->args[1]);
        return 0;
    }
    else if(daemPtr->state==status_active){
        sf_stop(daemPtr->args[1],daemPtr->pid);
        daemPtr->state = status_stopping;
        childPid = daemPtr->pid;
        // mask everything except SIGCHLD and SIGALRM
        sigset_t maskEverything;
        sigfillset(&maskEverything);
        sigset_t maskAllButSIGCHLDALRM;
        sigfillset(&maskAllButSIGCHLDALRM);
        sigdelset(&maskAllButSIGCHLDALRM,SIGCHLD);
        sigdelset(&maskAllButSIGCHLDALRM,SIGALRM);
        sigset_t prevMask;
        // mask everything and store previous mask
        if(sigprocmask(SIG_BLOCK,&maskEverything,&prevMask)==-1){
            sf_error("Error masking everything");
            return -1;
        }
        processKilled = 1;
        kill(daemPtr->pid,SIGTERM);
        alarm(CHILD_TIMEOUT);
        // wait for SIGCHLD or SIGALRM by unmasking just these two
        sigsuspend(&maskAllButSIGCHLDALRM);
        // waitpid(daemPtr->pid,&(daemPtr->exit_status),WNOHANG);
        alarm(0);
        // if a sigkill was sent wait for sigchld before proceeding
        if(processKilled != 1){
            sigsuspend(&maskAllButSIGCHLDALRM);
        }
        if(processKilled == -1){
            sf_error("Unable to kill child process after SIGKILL");
            return -1;
        }
        if(processKilled == 0){
            sf_kill(daemPtr->args[1],daemPtr->pid);
            // sf_crash(daemPtr->args[1], daemPtr->pid, SIGKILL);
            daemPtr->state=status_crashed;
            if(sigprocmask(SIG_SETMASK,&prevMask,NULL)==-1){
                sf_error("Error restoring previous mask");
                return -1;
            }
            return -1;
        }
        daemPtr->state = status_exited;
        // restore previous mask
        if(sigprocmask(SIG_SETMASK,&prevMask,NULL)==-1){
            sf_error("Error restoring previous mask");
            return -1;
        }
        if(processKilled == 0){
            sf_kill(daemPtr->args[1],daemPtr->pid);
            sf_error("SIGKILL killed program after SIGTERM did not respond in time");
            return -1;
        }
    }
    else{
        sf_error("Cannot stop a daemon that is not active");
        fprintf(out,"Cannot stop a daemon that is not active\n");
        fflush(out);
        return -1;
    }
    return 0;
}
// changes daemons[] for sigchld received and displays
void handleChildReap(daem* daemons[], int numDaemons,FILE* out){
    int exitStatus;
    pid_t childPid;
    daem* dPtr;
    if((childPid = waitpid(-1, &exitStatus,WNOHANG))==0){
        sf_error("Error reaping child");
        return;
    }
    else if(childPid == 0){
        sf_error("Unable to reap when there is no child available to reap");
        return;
    }
    else{
        for(int i = 0; i<=numDaemons; i++){
            if(i==numDaemons){
                sf_error("Cannot reap a child with pid not in daemons[]");
                return;
            }
            if(childPid == daemons[i]->pid){
                dPtr = daemons[i];
                break;
            }
        }
        if(WIFEXITED(exitStatus)){
            dPtr->state = status_exited;
            sf_term(dPtr->args[1],dPtr->pid,exitStatus);
        }
        else if(WIFSIGNALED(exitStatus)){
            dPtr->state = status_crashed;
            sf_crash(dPtr->args[1],dPtr->pid,WTERMSIG(exitStatus));
        }
        dPtr->pid = 0;
        dPtr->exit_status = exitStatus;
    }
}
// stop all daemons and then free all of the daemons in the struct
void quitLegion(daem* daemons[], int numDaemons,FILE* out){
    for(int i = 0; i<numDaemons; i++){
        if(daemons[i]->state == status_active){
            stopDaemon(daemons[i],out);
            handleChildReap(daemons,numDaemons,out);
            sigchldReceived = 0;
        }
    }
    for(int i = 0; i<numDaemons; i++){
        free_daemon(daemons[i]);
    }
}
void free_args(char* args[], int numArgs){
    for(int i = 0; i<numArgs; i++){
        free(args[i]);
    }
}
void run_cli(FILE *in, FILE *out)
{
    globalInFd = fileno(in);
    char* input = NULL;
    size_t inputSize = 0;
    ssize_t getlineErrorCheck;
    size_t inputBufferSize = 0;
    // declare array of pointers to daem structs
    // MAX SIZE OF DAEMONS ARRAY
    // args also has max value of 1024
    int daemonsArraySize = 10000;
    daem* daemons[daemonsArraySize];
    int numDaemons = 0;
    // install signal handlers
    if(signal(SIGINT,sigint_handler)==SIG_ERR){
        sf_error("error installing sigint handler");
        return;
    }
    if(signal(SIGALRM,sigalrm_handler)==SIG_ERR){
        sf_error("error installing sigalrm handler");
        return;
    }
    if(signal(SIGCHLD,sigchld_handler)==SIG_ERR){
        sf_error("error installing sigchld handler");
        return;
    }
    while(1){
        input = NULL;
        inputSize = 0;
        inputBufferSize = 0;
        getlineErrorCheck = 0;
        // check if signal was received before select is executed
        // can happen if signal is received during execution of command
        if(sigintReceived==1){
            quitLegion(daemons,numDaemons,out);
            break;
        }
        if(sigchldReceived==1){
            handleChildReap(daemons,numDaemons,out);
            sigchldReceived=0;
        }
        sf_prompt();
        fprintf(out, "legion> ");
        fflush(out);
        // block until signal or input is available by using select
        fd_set fdInSet;
        FD_ZERO(&fdInSet);
        FD_SET(globalInFd,&fdInSet);
        int retval;
        while((retval=select(globalInFd+1,&fdInSet,NULL,NULL,NULL))==-1 || retval == 0){
            // this means select was interrupted by signal, if so then check for SIGINT or SIGCHLD
            // after handling signal keep waiting for input or possibly another signal
            if(errno = EINTR){
                if(sigintReceived==1){
                    quitLegion(daemons,numDaemons,out);
                    break;
                }
                if(sigchldReceived==1){
                    handleChildReap(daemons,numDaemons,out);
                    sigchldReceived=0;
                    retval = 0;
                    sf_prompt();
                    fprintf(out, "legion> ");
                    fflush(out);
                }
            }
            else{
                sf_error("Error waiting for input using select()");
                break;
            }
        }
        if(sigintReceived==1){
            break;
        }
        // // check for error when reading line from input
        if((getlineErrorCheck=getline(&input, &inputBufferSize, in)) == -1){
            if(feof(in)){
                sf_error("EOF encountered");
                fprintf(out,"EOF encountered\n");
                fflush(out);
                quitLegion(daemons,numDaemons,out);
                break;
            }
            else{
                for(int i = 0; i<numDaemons; i++){
                    if(daemons[i]->state == status_active){
                        stopDaemon(daemons[i],out);
                    }
                }
                sf_error("error reading input");
                break;
            }
        }
        // if EOF is reached then the next call after executing will quit
        inputSize = strlen(input);
        // now separate the input into arguments
        char *args[1024];
        int argCounter = 0;
        char* tokenBeginning = input;
        char tokenBuffer[1024];
        // loop through input
        for(int i = 0; i<inputSize; i++){
            tokenBeginning = &input[i];
            // initialize buffer to empty string
            tokenBuffer[0] = '\0';
            // loop until valid space or end is encountered
            while(i<inputSize){
                if(input[i] == '\''){
                    // if quote is found, concatenate (tokenBeg up to quote) to buffer
                    input[i] = '\0';
                    strcat(tokenBuffer, tokenBeginning);
                    i++;
                    tokenBeginning = &input[i];
                    // then loop until newline or next quote (or null char for EOF) and concatenate to buffer
                    while(input[i] != '\n' && input[i] != '\'' && input[i] != '\0'){
                        i++;
                    }
                    if(input[i] == '\0' || input[i] == '\n'){
                        input[i] = '\0';
                        strcat(tokenBuffer, tokenBeginning);
                        break;
                    }
                    input[i] = '\0';
                    strcat(tokenBuffer, tokenBeginning);
                    // continue looping until another quote/space/newline is encountered if present and concatenate accordingly
                    i++;
                    tokenBeginning = &input[i];
                }
                // if a valid space or newline is encountered, concatenate strBeg up to this character and break
                if(input[i] == ' ' || input[i] == '\n'){
                    input[i] = '\0';
                    strcat(tokenBuffer, tokenBeginning);
                    break;
                }
                i++;
            }
            // at this point buffer should be filled correctly with one arg, malloc into args array
            args[argCounter] = malloc(strlen(tokenBuffer)+1);
            strcpy(args[argCounter], tokenBuffer);
            argCounter++;
        }
        args[argCounter] = NULL;
        free(input);
        daem* newDaemPtr = NULL;
        daem* daemPtr = NULL;
        // now check for correct arguments
        if(strcmp(args[0],"help")==0){
            fprintf(out,
            "Available commands:\nhelp (0 args) Print this help message\nquit (0 args) Quit the program\nregister (0 args) Register a daemon\nunregister (1 args) Unregister a daemon\nstatus (1 args) Show the status of a daemon\nstatus-all (0 args) Show the status of all daemons\nstart (1 args) Start a daemon\nstop (1 args) Stop a daemon\nlogrotate (1 args) Rotate log files for a daemon"
            );
            fflush(out);
        }
        else if(strcmp(args[0],"register")==0){
            if(argCounter<3){
                sf_error("Wrong number of args provided");
                fprintf(out,"Usage: register <daemon> <cmd-and-args>\n");
                fflush(out);
            }
            else{
                if(numDaemons == daemonsArraySize){
                    sf_error("Max daemons reached");
                    fprintf(out,"Max daemons reached");
                    fflush(out);
                }
                else{
                    // if nonempty and not filled list, check for same name daemons
                    daemPtr = returnDaem(args[1],daemons,numDaemons);
                    if(daemPtr== NULL){
                        // create new daemon and initialize values
                        newDaemPtr = malloc(sizeof(daem));
                        newDaemPtr->pid=0;
                        newDaemPtr->state = status_inactive;
                        newDaemPtr->exit_status = 0;
                        newDaemPtr->numArgs = argCounter;
                        memcpy(newDaemPtr->args,args,(argCounter+1) * sizeof(char*));
                        sf_register(args[1],args[2]);
                        daemons[numDaemons] = newDaemPtr;
                        numDaemons++;
                    }
                    else{
                        sf_error("Daemon is already registered");
                        fprintf(out,"Daemon %s is already registered\n",daemPtr->args[1]);
                        fflush(out);
                    }
                }
            }
        }
        else if(strcmp(args[0],"unregister")==0){
            if(argCounter!=2){
                sf_error("Wrong number of args provided");
                fprintf(out,"Wrong number of args given (given: %d, required: 1)for unregister\n",argCounter-1);
                fflush(out);
            }
            else{
            // iterate to find daemon to unregister
            daemPtr = returnDaem(args[1], daemons, numDaemons);
            if(daemPtr == NULL){
                sf_error("Daemon does not exist");
                fprintf(out,"Daemon %s is not registered\n",args[1]);
                fflush(out);
            }
            else{
                // first check for inactive state
                if(daemPtr->state != status_inactive){
                    sf_error("Daemon exists but is not inactive");
                    fprintf(out,"Daemon is not inactive\n");
                    fflush(out);
                }
                else{
                    int daemIndex = returnDaemIndex(args[1], daemons, numDaemons);
                    unregister_daemon(daemons,daemIndex,&numDaemons);
                }
                sf_unregister(args[1]);
            }
            free_args(args,argCounter);
            }
        }
        else if(strcmp(args[0],"status")==0){
            if(argCounter!=2){
                sf_error("Wrong number of args provided");
                fprintf(out,"Wrong number of args given (given: %d, required: 1)for status\n",argCounter-1);
                fflush(out);
            }
            else{
                // search for daemon to print
                daemPtr = returnDaem(args[1], daemons, numDaemons);
                if(daemPtr==NULL){
                    sf_error("Daemon not found");
                    fprintf(out,"Daemon %s does not exist\n",args[1]);
                    fflush(out);
                }
                else{
                    print_status(daemPtr,out);
                }
                free_args(args,argCounter);
            }
        }
        else if(strcmp(args[0],"status-all")==0){
            if(argCounter!=1){
                sf_error("Wrong number of args provided");
                fprintf(out,"Wrong number of args given (given: %d, required: 0)for status-all\n",argCounter-1);
                fflush(out);
            }
            else{
                for(int i = 0; i<numDaemons; i++){
                    print_status(daemons[i],out);
                }
            }
            free_args(args,argCounter);
        }
        else if(strcmp(args[0],"start")==0){
            if(argCounter!=2){
                sf_error("Wrong number of args provided");
                fprintf(out,"Wrong number of args given (given: %d, required: 1) for start\n",argCounter-1);
                fflush(out);
            }
            else{
                daemPtr = returnDaem(args[1],daemons,numDaemons);
                if(daemPtr==NULL){
                    sf_error("Daemon not found");
                    fprintf(out,"Daemon %s does not exist\n",args[1]);
                    fflush(out);
                }
                else{
                    if(daemPtr->state!=status_inactive){
                        sf_error("Status not inactive");
                        fprintf(out,"Status not inactive\n");
                        fflush(out);
                    }
                    else{
                        startDaemon(daemPtr);
                    }
                }
            }
            free_args(args,argCounter);
        }
        else if(strcmp(args[0],"stop")==0){
            if(argCounter!=2){
                sf_error("Wrong number of args provided");
                fprintf(out,"Wrong number of args given (given: %d, required: 1)for stop\n",argCounter-1);
                fflush(out);
            }
            else{
                daemPtr = returnDaem(args[1],daemons,numDaemons);
                if(daemPtr==NULL){
                    sf_error("Daemon not found");
                    fprintf(out,"Daemon %s does not exist\n",args[1]);
                    fflush(out);
                }
                else{
                    stopDaemon(daemPtr,out);
                }
            }
            free_args(args,argCounter);
        }
        else if(strcmp(args[0],"logrotate")==0){
            if(argCounter!=2){
                sf_error("Wrong number of args provided");
                fprintf(out,"Wrong number of args given (given: %d, required: 1)for logrotate\n",argCounter-1);
                fflush(out);
            }
            else{
                daem* logDPtr;
                if((logDPtr =returnDaem(args[1],daemons,numDaemons))==NULL){
                    sf_error("Daemon not found");
                    fprintf(out,"Daemon %s does not exist\n",args[1]);
                    fflush(out);
                }
                else if(logDPtr->state != status_active){
                    sf_error("Cannot logrotate a daemon that is not active");
                    fprintf(out,"Cannot logrotate a daemon that is not active\n");
                    fflush(out);
                }
                else{
                    sf_logrotate(args[1]);
                    // delete max number log if it exists
                    char logPath[1024];
                    char newLogPath[1024];
                    newLogPath[0] = '\0';
                    char number[LOG_VERSIONS];
                    number[0] = '\0';
                    char newNumber[LOG_VERSIONS];
                    newNumber[0] = '\0';
                    snprintf(number,sizeof(number),"%d",LOG_VERSIONS);
                    createNewLogPath(logPath,args[1],number);
                    unlink(logPath);
                    // rename all of the logs up to LOG_VERSIONS
                    for(int i = LOG_VERSIONS; i>=0; i--){
                        snprintf(number,sizeof(number),"%d",i);
                        snprintf(newNumber,sizeof(newNumber),"%d",i+1);
                        createNewLogPath(logPath,args[1],number);
                        createNewLogPath(newLogPath,args[1],newNumber);
                        rename(logPath,newLogPath);
                    }
                    // now restart the daemon which will create the log file
                    // check for SIGCHLD on stopping
                    stopDaemon(logDPtr,out);
                    if(sigchldReceived==1){
                        handleChildReap(daemons,numDaemons,out);
                        sigchldReceived=0;
                    }
                    startDaemon(logDPtr);
                }
            }
            free_args(args,argCounter);
        }
        else if(strcmp(args[0],"quit")==0){
            quitLegion(daemons,numDaemons,out);
            free_args(args,argCounter);
            break;
        }
        else{
            sf_error("Error executing command");
            free_args(args,argCounter);
        }
    }
}
