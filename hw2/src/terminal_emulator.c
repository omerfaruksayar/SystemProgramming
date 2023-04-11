#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>

#define MAX_COMMANDS 20
#define MAX_ARGS 20

int parse_commands(char* input, char** commands) {
    int num_commands = 0;
    char* command = strtok(input, "|");
    while (command != NULL && num_commands < MAX_COMMANDS) {
        commands[num_commands] = command;
        num_commands++;
        command = strtok(NULL, "|");
    }
    return num_commands;
}

int parse_arguments(char* command, char** arguments) {
    int num_args = 0;
    char* arg = strtok(command, " ");
    //arg =  strtok(NULL, " ");
    while (arg != NULL && num_args < MAX_ARGS) {
        arguments[num_args] = arg;
        num_args++;
        arg = strtok(NULL, " ");
    }
    arguments[num_args] = NULL;
    return num_args;
}

void usage() {
    printf("Usage: command [arg ...]\n");
}   

int main() {
    char input[100];
    char* commands[MAX_COMMANDS];
    char* arguments[MAX_ARGS];
    int num_commands, num_args,in_fd, out_fd;

    while (1) {
        printf("$ ");
        fflush(stdout);
        fgets(input, 100, stdin);

        if (strcmp(input, ":q\n") == 0)
            break;   
        
        input[strcspn(input, "\n")] = 0; // remove newline character

        num_commands = parse_commands(input, commands);

        int pipefd[num_commands-1][2];

        for (int i = 0; i < num_commands-1; i++)
            pipe(pipefd[i]);

        for (int i = 0; i < num_commands; i++) {
            num_args = parse_arguments(commands[i], arguments);
            
            switch (fork())
            {
                case -1:
                    perror("fork");

                case 0:
                    
                    
                    for (size_t k = 0; k < num_args; k++)
                    {
                        if (strcmp(arguments[k],"<") == 0)
                        {
                            in_fd = open(arguments[k+1], O_RDONLY);
                            if (in_fd == -1)
                                perror("open");
                            
                            dup2(in_fd, STDIN_FILENO);
                            close(in_fd);
                            arguments[k] = NULL;
                            arguments[k+1] = NULL;
                            break;    
                        }

                        if (strcmp(arguments[k],">") == 0)
                        {
                            out_fd = open(arguments[k+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                            dup2(out_fd, STDOUT_FILENO);
                            close(out_fd);
                            arguments[k] = NULL;
                            arguments[k+1] = NULL;
                            break;
                        }
                        
                    }
                    
                    execvp(arguments[0], arguments);
                    perror("execvp");

                default:
                    wait(NULL);
                    break;            
            }
        }
    }  

    return 0;
}
