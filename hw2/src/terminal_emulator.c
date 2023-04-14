#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>

#define MAX_COMMANDS 20
#define MAX_ARGS 20
#define MAX_INPUT 1024

int num_commands;
char input[MAX_INPUT];
char* commands[MAX_COMMANDS];
char* arguments[MAX_ARGS];
pid_t pids[MAX_COMMANDS];
int pipes[MAX_COMMANDS-1][2];
static volatile int alive = 1;

void parse_commands(char* input, char** commands) {
    num_commands = 0;
    char* command = strtok(input, "|");

    while (command != NULL) {
        commands[num_commands] = command;
        num_commands++;
        command = strtok(NULL, "|");
    }
}

int parse_arguments(char* command, char** arguments) {
    int num_args = 0;
    char* arg = strtok(command, " ");
   
    while (arg != NULL) {
        arguments[num_args] = arg;
        num_args++;
        arg = strtok(NULL, " ");
    }

    arguments[num_args] = NULL;
    return num_args;
}

void reset(){
    fflush(stdin);
    fflush(stdout);
    memset(arguments, 0, MAX_ARGS * sizeof(char*));
    memset(commands, 0, MAX_COMMANDS * sizeof(char*));
    memset(pipes, 0, (MAX_COMMANDS-1) * sizeof(int[2]));
    memset(input, 0, MAX_INPUT * sizeof(char));
    memset(pids, 0, MAX_COMMANDS * sizeof(pid_t));
    num_commands = 0;
}

void usage() {
    printf("Usage: command [arg ...], for exit type :q (You can give max 20 commands with max 20 arguments) (Only pipe '|' and redirection operators '<', '>' are supported)\n");
} 

void sig_handler(int signum) {

    if(num_commands > 0){
        printf("\nCaught signal %d, killing child processes...\n", signum);
        for (int i = 0; i < num_commands; i++) {
            if (pids[i] != 0) {
                kill(pids[i], SIGTERM);
                waitpid(pids[i], NULL, 0);
                pids[i] = 0;
            }
        }
    }

    //When signal comes before any command execute
   else
        printf("\nCaught signal %d\n$ ", signum);

    reset();

}

void quit_handler(int signum){

    if(signum)
        printf("Caught signal %d, quiting...\n", signum);
    
    else
        printf("Quiting...\n");

    for (int i = 0; i < num_commands; i++) {
        if (pids[i] != 0) {
            kill(pids[i], SIGTERM);
            waitpid(pids[i], NULL, 0);
            pids[i] = 0;
        }
    }
    alive = 0;
}

int main(int argc, char* argv[]) {

    if (argc != 1) {
        usage();
        exit(EXIT_FAILURE);
    }
    
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGTSTP, sig_handler);
    signal(SIGQUIT, quit_handler);
    int num_args, in_fd, out_fd;

    while (alive) {
        
        printf("$ ");
        reset();
        if (fgets(input, MAX_INPUT, stdin) == NULL)
        {
            quit_handler(0);
            break;
        }
        
        input[strcspn(input, "\t")] = 0; // remove tab character
        input[strcspn(input, "\n")] = 0; // remove newline character

        if (strcmp(input,":q") == 0){
            quit_handler(0);
            break;        
        }
            
        parse_commands(input, commands);

        if (num_commands == 0 || num_commands > 20){
            usage();
            continue;
        }

        for (int i = 0; i < num_commands-1; i++){
            if (pipe(pipes[i]) == -1){
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }

        // Fork child processes
        for (int i = 0; i < num_commands; i++) {
            num_args = parse_arguments(commands[i], arguments);
            pids[i] = fork();
            
            switch (pids[i])
            {
                case -1:
                    perror("fork");
                    exit(EXIT_FAILURE);

                case 0:

                    if (i > 0){
                        // Duplicate read end to stdin
                        if(dup2(pipes[i-1][0], 0) == -1){
                            perror("dup2");
                            exit(EXIT_FAILURE);
                        }
                    }

                    if (i < num_commands - 1)
                        // Duplicate write end to stdout
                        if(dup2(pipes[i][1], 1) == -1){
                            perror("dup2");
                            exit(EXIT_FAILURE);
                        }

                    // Close all pipes end we're using was safely copied
                    for (int j = 0; j < num_commands - 1; j++) {
                        close(pipes[j][0]);
                        close(pipes[j][1]);
                    }

                    int index[2] = {0,0};

                    for (int k = 1; k < num_args; k++)
                    {
                        if (strcmp(arguments[k],"<") == 0)
                        {   
                            if (arguments[k+1] == NULL)
                            {
                                printf("No input file specified!\n");
                                exit(EXIT_FAILURE);
                            }
                            

                            in_fd = open(arguments[k+1], O_RDONLY);

                            if (in_fd == -1){
                                perror("open");
                                exit(EXIT_FAILURE);
                            }
                            
                            dup2(in_fd, 0);
                            close(in_fd);
                            index[0] = k;
                      
                        }

                        if (strcmp(arguments[k],">") == 0)
                        {   
                            if (arguments[k+1] == NULL)
                            {
                                printf("No output file specified!\n");
                                exit(EXIT_FAILURE);
                            }

                            out_fd = open(arguments[k+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);

                            if (out_fd == -1){
                                perror("open");
                                exit(EXIT_FAILURE);
                            }

                            dup2(out_fd, 1);
                            close(out_fd);
                            index[1] = k;
                        }   
                    }

                    if (index[0] != 0)
                    {
                        arguments[index[0]] = NULL;
                        arguments[index[0]+1] = NULL;
                    }

                    if (index[1] != 0)
                    {
                        arguments[index[1]] = NULL;
                        arguments[index[1]+1] = NULL;
                    }
                    

                    if (execvp(arguments[0], arguments) == -1)
                    {
                        perror("execvp");
                        exit(EXIT_FAILURE);
                    }

                default:
                    break;            
            }
        }

        // Close all pipe descriptors
        for (int i = 0; i < num_commands - 1; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }

        // Wait for all child processes to finish
        for (int i = 0; i < num_commands; i++) {
            waitpid(pids[i], NULL, 0);
        }

        // Write to log file
        time_t now = time(NULL);
        char timestamp[20];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d_%H:%M:%S", localtime(&now));
        char filename[30];
        snprintf(filename, sizeof(filename), "log_%s.txt", timestamp);
        FILE *fp = fopen(filename, "w");

        if (fp != NULL) {

            for (int i = 0; i < num_commands; i++) {
                if (pids[i] != 0) {
                    fprintf(fp, "Command: %s, PID: %d\n", commands[i], pids[i]);
                }
            }
            fclose(fp);
        } 
        else 
            perror("fopen");
    }

    return 0;
}