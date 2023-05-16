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

    int connection_status;
    ssize_t bytes =  read(client_fifo_fd, &connection_status, sizeof(connection_status));
    if (bytes == sizeof(connection_status)) {
        if (connection_status == -1)
        {
            printf("Connection request rejected\n");
            exit(0);
        }
        
        printf("Connection established with server\n");

        if(close(client_fifo_fd) == -1){
            perror("close");
            exit(1);
        }

        while (1)
        {   
            // open client fifo for writing
            int client_fifo_fd = open(client_fifo, O_WRONLY);
            if (client_fifo_fd == -1) {
                perror("open client fifo");
                exit(1);
            }

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

            else if(strcmp(command,"list") == 0){
                req.request = LIST;
                if (write(client_fifo_fd, &req, sizeof(Request)) == -1) {
                    perror("write");
                    continue;
                }

                if(close(client_fifo_fd) == -1){
                    perror("close");
                    exit(1);
                }

                client_fifo_fd = open(client_fifo, O_RDONLY);
                if (client_fifo_fd == -1) {
                    perror("open client fifo");
                    exit(1);
                }

                char buf[256];
                memset(buf, 0, sizeof(buf));

                while (read(client_fifo_fd, &buf, sizeof(buf)) != 0)
                {
                    printf("%s", buf);
                    memset(buf, 0, sizeof(buf));
                }

                if(close(client_fifo_fd) == -1){
                    perror("close");
                    exit(1);
                }
            }

            else if (strcmp(command,"readF") == 0){

                char* filename = strtok(NULL, " ");
                if (filename == NULL)
                {
                    printf("Usage: readF <file> <line #>\n");
                    continue;
                }

                char* lineNum = strtok(NULL, " ");
                if (lineNum == NULL)
                {
                    printf("Usage: readF <file> <line #>\n");
                    continue;
                }

                req.request = READF;
                strcpy(req.filename, filename);
                req.filename[strlen(filename)] = '\0';
                req.offset = atoi(lineNum);

                if (write(client_fifo_fd, &req, sizeof(Request)) == -1) {
                    perror("write");
                    continue;
                }

                if(close(client_fifo_fd) == -1){
                    perror("close");
                    exit(1);
                }

                client_fifo_fd = open(client_fifo, O_RDONLY);
                if (client_fifo_fd == -1) {
                    perror("open client fifo");
                    exit(1);
                }

                char buf[BUFSIZE];
                memset(buf, 0, sizeof(buf));

                while (read(client_fifo_fd, &buf, sizeof(buf)) != 0)
                {
                    printf("%s", buf);
                    memset(buf, 0, sizeof(buf));
                }

                printf("\n");

                if(close(client_fifo_fd) == -1){
                    perror("close");
                    exit(1);
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
