#define _POSIX_C_SOURCE 200112

/** 
 * Smallshell 2.51 for UNIX 
 **/

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* if this is non-zero, signals will be used to detect terminated
 * background-processes. */
#define SIGNALDETECTION 1

/* max line length 70, plus one for null terminator */
#define MAX_COMMAND_SIZE 71

/* five plus one for the first parameter, the command itself */
#define MAX_NO_ARGS 6

/* registers a signal handler */
void register_sighandler(int signal_code, void (*handler)(int, siginfo_t*, void*)) {
    int return_value;
    struct sigaction signal_parameters;
    signal_parameters.sa_sigaction = handler; 
    sigemptyset( &signal_parameters.sa_mask );
    signal_parameters.sa_flags = SA_SIGINFO;
    return_value = sigaction(signal_code, &signal_parameters, (void *) NULL);
    if ( -1 == return_value ) {
        fprintf(stderr,"sigaction() failed\n");
        exit(1);
    }
}

void print_exit_msg(pid_t child_pid) {
    printf("==> %d - process terminated\n", (int)child_pid);
}

/* this function is called when SIGCHLD is received */
void signal_handler(int signal_code, siginfo_t* siginfo, void* ucontext) {
    int status;
    pid_t child_pid = waitpid(siginfo->si_pid, &status, WNOHANG);

    /*
     * We can end up here in case of SIGCHLD from a foreground OR background
     * process. When a fg process signals SIGCHLD, we have already waited for
     * it, which will cause waitpid(2) to return -1, so we simply ignore that
     * case. When a bg process signals SIGCHLD we have to make sure that it has
     * really changed state, that is, waitpid(2) returns the process ID of the
     * child.
     */
    if (child_pid == siginfo->si_pid) {
        print_exit_msg(siginfo->si_pid);
    }
}

/* main function, handles main program flow */
int main () {

    /* keep track of child process while forking */
    pid_t child_pid;

    /* helps us time processes */
    long long timediff;

    /* foreground or background branch? */
    int FOREGROUND;

    /* user-specified command */
    char command[MAX_COMMAND_SIZE];
    
    /* command arguments */
    char * command_args[MAX_COMMAND_SIZE];

    /* char pointer used for string parsing */
    char * pchr;
        
    /* variable to keep track of which parameter we're at while parsing */
    int param_index = 0;

    /* keep track of child process status */
    int child_status;

    /* structs used to track time */
    struct timeval time1,time2;

    /* temporary variable to track return values of functions */
    int return_value;
                
    /* temporary variable to track return values of functions */
    char * return_value_ptr;
    
    /* ignore sig-interrupt (Ctrl-C) in the parent process */
    signal(SIGINT,SIG_IGN);
    
    if (SIGNALDETECTION) {
        /* install handler for SIGCHLD */
        register_sighandler(SIGCHLD, signal_handler);
    }

    /* main program loop */
    for (;;) {
        
        /* get command string */
        return_value_ptr = fgets(command,MAX_COMMAND_SIZE,stdin);

        /* if EINTR, no nothing */
        if (return_value_ptr == NULL && errno == EINTR)
            continue;

        /* empty line (only newline), do nothing */
        if (strlen(command) == 1) 
            continue;

        /* if last char is '&', set FOREGROUND to 0, otherwise set it to 1 */
        if (strlen(command) >= 2 && command[strlen(command)-2] == '&') {
            /* replace '&' with '\0' */
            command[strlen(command)-2] = '\0';
            FOREGROUND = 0;
        } else {
            FOREGROUND = 1;
        }

        /* replace newline character with null terminator */
        command[strlen(command)-1] = '\0';

        /* check for exit command */
        if (!strcmp(command,"exit"))
            break;

        /* split command string at spaces */
        pchr = strtok(command," ");

        /* first parameter is always the program itself */
        param_index = 0;
        command_args[param_index] = pchr;
        ++param_index;
        
        /* go through and save parameters into string array */
        while (pchr != NULL) {
            pchr = strtok(NULL," ");
            command_args[param_index] = pchr;
            ++param_index;
        }
        
        /* check for cd command */
        if (!strncmp(command,"cd",2)) {
            /* cd always takes 3 parameters ("cd cd example/directory/") */
            if (param_index != 3) {
                printf("==> ERROR: Invalid argument count to cd!\n");
                continue;
            }

            /* check the return value from chdir */
            return_value = chdir(command_args[1]);
            if (return_value == -1) {

                /* the directory is not valid */
                if (ENOTDIR) {
                    printf("==> ERROR: Invalid directory, sending you home...\n");
                    chdir(getenv("HOME"));

                /* another error, unknown... */
                } else {
                    printf("==> ERROR: Couldn't change directory to %s\n",command_args[1]);
                }
            } 

            /* don't run rest of loop */
            continue;
        } 

        /* check no. of arguments */
        if (param_index > MAX_NO_ARGS)
            fprintf(stderr,"Error: Too many arguments!");

        /* executed for foreground process execution */
        if (FOREGROUND) {
            child_pid = fork();
            if (child_pid == 0) {
                /* unignore sig-interrupt in the child process (Ctrl-C) */
                signal(SIGINT,SIG_DFL);

                /* execute command with specified arguments */
                (void)execvp(command,command_args);
                
                printf("==> ERROR: Could not execute command: %s\n",command);
                exit(1);
            } else if (child_pid == -1) {
                /* couldn't fork, something is wrong */
                fprintf(stderr,"==> ERROR: Couldn't fork!"); 
                exit(1);
            } 
            
            /* this is executed by the parent process, we want to wait for
             * the child process to finish execution, and wait for its finished
             * execution */

            /* get first time measurement */
            gettimeofday(&time1,NULL);

            printf("==> %d - spawned foreground process\n",(int)child_pid);

            /* wait for child to finish */
            child_pid = waitpid(child_pid,&child_status,0);

            /* get second time measurement */
            gettimeofday(&time2,NULL);

            /* calculate time difference in microseconds */
            timediff = ((time2.tv_sec*1e6+time2.tv_usec)-(time1.tv_sec*1e6+time1.tv_usec));

            /* do not print if SIGNALDETECTION is set, would print two messages */
            if (!SIGNALDETECTION)
                print_exit_msg(child_pid);

            printf("==> execution time: %lf seconds\n",timediff/1e6);

        } else { /* background process execution */
            child_pid = fork();
            if (child_pid == 0) {
                /* unignore sig-interrupt in child (Ctrl-C) */
                signal(SIGINT,SIG_DFL);
                
                /* execute command with specified arguments */
                (void)execvp(command,command_args);
                
                printf("==> ERROR: Could not execute command: %s\n",command);
                exit(1);
            } else if (child_pid == -1) {
                /* unknown forking error, shouldn't happen */
                fprintf(stderr,"==> ERROR: Couldn't fork!"); exit(1);
            } 
            
            /* this is executed by the parent process */
            printf("==> %d - spawned background process\n",(int)child_pid);

        }

        /* in each iteration of the main program loop, we check if any programs
         * finished execution by polling, if SIGNALDETECTION == 0 */
        if (SIGNALDETECTION == 0) {
            for (;;) {
                /* check if any process has terminated, without hanging */
                child_pid = waitpid(-1,&child_status,WNOHANG);            

                /* if nothing has changed, or if error, break the loop */
                if (child_pid == 0 || child_pid == -1) {
                    break;
                }

                /* here, a process has terminated */
                print_exit_msg(child_pid);
            }
        }
    }
    return 0;
}
