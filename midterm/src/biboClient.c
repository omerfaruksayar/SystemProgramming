#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#include "../include/common.h"

#define BUFSIZE 1024
int serv_child_fifo_fd;

int main(int argc, char *argv[]) {
    // parse command-line arguments
    if (argc != 3) {
        printf("Usage: %s <Connect/tryConnect> ServerPID\n", argv[0]);
        exit(1);
    }
    pid_t server_pid = atoi(argv[2]);

    // create connection request struct
    connectionReq req;
    req.client_pid = getpid();
    if (strcmp(argv[1], "connect") == 0)
        req.try_flag = 0; // client will wait in the queue
    
    else if (strcmp(argv[1], "tryConnect") == 0)
        req.try_flag = 1; // client will not wait in the queue

    else {
        printf("Usage: %s <Connect/tryConnect> ServerPID\n", argv[0]);
        exit(1);
    }

    char client_fifo[256];
    snprintf(client_fifo, sizeof(client_fifo), "/tmp/client.%d", getpid());
    if (mkfifo(client_fifo, 0666) == -1) {
        perror("mkfifo client");
        exit(1);
    }

    char server_fifo[256];
    snprintf(server_fifo, sizeof(server_fifo), "/tmp/server.%d", server_pid);
    // send connection request to server
    int server_fifo_fd = open(server_fifo, O_WRONLY);
    if (server_fifo_fd == -1) {
        perror("open server fifo");
        exit(1);
    }
    if (write(server_fifo_fd, &req, sizeof(connectionReq)) == -1) {
        perror("write");
        exit(1);
    }

    int client_fifo_fd = open(client_fifo, O_RDONLY);
    if (client_fifo_fd == -1) {
        perror("open client fifo");
        exit(1);
    }

    pid_t child_server_pid;
    ssize_t bytes =  read(client_fifo_fd, &child_server_pid, sizeof(child_server_pid));
    if (bytes == sizeof(child_server_pid)) {
        if (child_server_pid == -1)
        {
            printf("Connection request rejected\n");
            exit(0);
        }
        
        printf("Connection established with server %d\n", child_server_pid);
        char serv_child_fifo[256];
        snprintf(serv_child_fifo, sizeof(serv_child_fifo), "/tmp/server.%d", child_server_pid);
        // open child server FIFO for writing
        sleep(0.1);
        serv_child_fifo_fd = open(serv_child_fifo, O_WRONLY);
        if (serv_child_fifo_fd == -1) {
            perror("open child server fifo");
            exit(1);
        }

        while (1)
        {   
            Request req;
            printf("Enter a command: ");
            fflush(stdout);
            char input[BUFSIZE];
            fgets(input, sizeof(input), stdin);
            input[strlen(input) - 1] = '\0';
            //parse command according to space character
            char *token = strtok(input, " ");
            if (token == NULL)
                continue;

            char* command = malloc(strlen(token) + 1);
            strcpy(command, token);
            command[strlen(token)] = '\0';

            if (strcmp(command, "help") == 0)
            {
                printf("Possible commands:\n");
                printf("list\n");
                printf("readF <file> <line #>\n");
                printf("writeT <file> <line #> <string>\n");
                printf("upload <file>\n");
                printf("download <file>\n");
                printf("exit\n");                
            }

            if(strcmp(command,"list") == 0){
                req.request = LIST;
                req.message = NULL;
                if (write(serv_child_fifo_fd, &req, sizeof(Request)) == -1) {
                    perror("write");
                    continue;
                }

                if(read(client_fifo_fd, &req, sizeof(Request)) == -1){
                    perror("read");
                    continue;
                }

                printf("List of files:\n");
                while (1)
                {
                    //parse req.message according to space character and print each token
                    char *token = strtok(req.message, " ");
                    if (token == NULL)
                        break;
                    printf("%s\n", token);
                    free(req.message);
                }
                
            }  
        }
    }

    else 
    {   
        perror("read");
        exit(1); 
    }

    // clean up
    close(server_fifo_fd);
    close(client_fifo_fd);
    unlink(client_fifo);

    return 0;
}
