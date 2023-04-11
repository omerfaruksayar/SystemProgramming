#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_ARGS 20
#define MAX_LINE_LENGTH 1024

void print_usage() {
    printf("Usage: command [arg ...]\n");
}

void handle_signal(int signum) {
    printf("Caught signal %d\n", signum);
    exit(signum);
}

int main(int argc, char *argv[]) {
    char line[MAX_LINE_LENGTH];
    char *args[MAX_ARGS];
    int num_args, status;
    pid_t pid;
    time_t current_time;
    char log_file_name[64];
    FILE *log_file;

    // register signal handlers
    signal(SIGINT, handle_signal); 
    signal(SIGQUIT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGTSTP, handle_signal);

    while (1) {
        printf("> ");
        fflush(stdout);

        if (!fgets(line, MAX_LINE_LENGTH, stdin)) {
            printf("\n");
            exit(0);
        }

        // remove newline character
        line[strlen(line) - 1] = '\0';

        // check for :q to exit program
        if (strcmp(line, ":q") == 0)
            exit(0);
        
        // parse arguments
        num_args = 0;
        args[num_args++] = strtok(line, " ");
        while ((args[num_args] = strtok(NULL, " ")) && num_args < MAX_ARGS) {
            num_args++;
        }

        // check for empty line
        if (num_args == 0) {
            continue;
        }

        // create log file with current timestamp
        current_time = time(NULL);
        strftime(log_file_name, 64, "log_%Y%m%d_%H%M%S.txt", localtime(&current_time));
        log_file = fopen(log_file_name, "w");

        // execute commands in child processes
        pid = fork();
        if (pid == 0) {
            // child process
            int i, in_fd, out_fd, pipe_fd[2];
            char *cmd_args[MAX_ARGS];

            // redirect input from file if necessary
            in_fd = 0;
            for (i = 0; i < num_args; i++) {
                if (strcmp(args[i], "<") == 0) {
                    args[i] = NULL;
                    in_fd = open(args[i+1], O_RDONLY);
                    if (in_fd < 0) {
                        fprintf(stderr, "Error: cannot open input file '%s': %s\n", args[i+1], strerror(errno));
                        exit(1);
                    }
                    break;
                }
            }

            // redirect output to file if necessary
            out_fd = 1;
            for (i = 0; i < num_args; i++) {
                if (strcmp(args[i], ">") == 0) {
                    args[i] = NULL;
                    out_fd = open(args[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0666);
                    if (out_fd < 0) {
                        fprintf(stderr, "Error: cannot open output file '%s': %s\n", args[i+1], strerror(errno));
                        exit(1);
                    }
                    break;
                }
            }

            // handle pipes if necessary
            for (i = 0; i < num_args; i++) {
                if (strcmp(args[i], "|") == 0) {
                    args[i] = NULL;
                    if (pipe(pipe_fd) < 0) {
                        fprintf(stderr, "Error: cannot create pipe: %s\n", strerror(errno));
                        exit(1);
                    }
                    pid_t pipe_pid = fork();
                    if (pipe_pid == 0) {
                        // child process for second command in pipe
                        dup2(pipe_fd[0], 0); // redirect input from pipe
                        close(pipe_fd[1]);
                        execvp(args[i+1], args+i+1);
                        fprintf(stderr, "Error: cannot execute command '%s': %s\n", args[i+1], strerror(errno));
                        exit(1);
                    } else if (pipe_pid < 0) {
                        fprintf(stderr, "Error: cannot create child process for command '%s': %s\n", args[i+1], strerror(errno));
                        exit(1);
                    }
                    // parent process for first command in pipe
                    dup2(pipe_fd[1], 1); // redirect output to pipe
                    close(pipe_fd[0]);
                    execvp(args[0], args);
                    fprintf(stderr, "Error: cannot execute command '%s': %s\n", args[0], strerror(errno));
                    exit(1);
                }
            }

            // execute command
            for (i = 0; i < num_args; i++) {
                cmd_args[i] = args[i];
            }
            cmd_args[i] = NULL;
            if (in_fd != 0) {
                dup2(in_fd, 0);
                close(in_fd);
            }
            if (out_fd != 1) {
                dup2(out_fd, 1);
                close(out_fd);
            }
            execvp(args[0], cmd_args);
            fprintf(stderr, "Error: cannot execute command '%s': %s\n", args[0], strerror(errno));
            exit(1);
            } 
            
        else if (pid < 0) {
            fprintf(stderr, "Error: cannot create child process for command '%s': %s\n", args[0], strerror(errno));
            continue;
        }

        // parent process
        fprintf(log_file, "%d %s\n", pid, line);
        fclose(log_file);
        wait(&status);
    }
}