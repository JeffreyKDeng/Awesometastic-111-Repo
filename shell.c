#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

#define EXIT_SUCCESS 0
#define TRUE 1
#define FALSE 0

extern char **get_line(void);

void signal_handler(int signo);
void command(char **);
void execute(char **args, int bg);
void waitywait(int seconds);

int main(int argc, char *argv[]) {
    char **args; 

    /* setup signal handlers */)
    if(signal(SIGCHLD, signal_handler) == SIG_ERR) 
        printf("Error catching SIGCHLD \n");
    if(signal(SIGINT, signal_handler) == SIG_ERR){
        printf("Error catching SIGINT \n");
    }

    while(1) {
        printf("> ");
        args = get_line();
        if(!args) break;
        if(args[0] != NULL){ 
            command(args);
        }
    }
    return 0;
}

void command(char ** args){

    /* Whether to run in background or not */
    int bg = FALSE;

    /* file pointers */
    FILE *out = NULL;
    FILE *in = NULL;

    /* copying stdin and stdout to reopen later */
    int in_dup = dup(0);
    int out_dup = dup(1);

    /* iterate through input string */
    int i;

    /* keep track of the first redirect */
    int redir = -1;

    /* Check for program exit */
    if(!strcmp(args[0], "exit")) exit(EXIT_SUCCESS);

    for(i = 0; args[i] != NULL; i++){
        
        if(!strcmp(args[i], "&")){
            args[i] = NULL;
            bg = TRUE;
            break;
        }

        else if(!strcmp(args[i], ">")){
            if(args[i+1] != NULL){

                /* If we haven't already found a first 
                    redirect marker, we set it here */
                if(redir == -1) redir = i;

                /* Open file to write to */
                out = freopen(args[i+1], "w", stdout);
                if(out == NULL) {
                    printf("file could not be opened for write\n");
                    fclose(stdout);
                    dup2(out_dup, 1);
                    return;
                }
            }
        }

        /* see the analogous 'else if' statement
            above for relevant comments */
        else if(!strcmp(args[i], "<")){
            if(args[i+1] != NULL){  
                if(redir == -1) redir = i;  
                in = freopen(args[i+1], "r", stdin);
                if(in == NULL) {
                    printf("file could not be opened for read\n");
                    fclose(stdin);
                    dup2(in_dup, 0);
                    return;
                }
            }
        }
    }

    /* we've already redirected relevant filestreams,
        so we just need to end the string at the proper place */
    if(redir != -1) args[redir] = NULL;
    execute(args, bg);

    /* close stdin and stdout and reopen as correct file streams*/
    if(out != NULL){
        dup2(out_dup, 1);
        printf("success \n");
    }
    if(in != NULL){
        dup2(in_dup, 0);
    }

}

void waitywait(int seconds){
    sleep(seconds);
    printf("HI THERE! \n");
}

void execute(char ** args, int bg){

    /* create a pid_t to hold the child process id */
    pid_t pid;

    /* this will hold the status of any child processes */
    int status;
    
    if((pid = fork()) < 0){
        printf("Failed to fork \n");
        exit(1);
    }
    else if(pid > 0){
        if(!bg) {
            waitpid(pid, &status, 0);
        }
    }
    else{
        if(!strcmp(args[0], "waitywait")) waitywait(10);
        else if(execvp(args[0], args) < 0) {
            printf("No such program exists. \n");
            exit(1);
        }
        exit(0);
    }
}

void signal_handler(int signo){
    int status;
    if(signo == SIGCHLD){
        wait(&status);
        if(status != 0) printf("An error occurred in execution. \n");
    }
}