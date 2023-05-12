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


    char server_fifo[256];
    snprintf(server_fifo, sizeof(server_fifo), "/tmp/server.%d", server_pid);
    // send connection request to server
    int server_fifo_fd = open(server_fifo, O_WRONLY);
    if (server_fifo_fd == -1) {
        perror("open");
        exit(1);
    }
    if (write(server_fifo_fd, &req, sizeof(connectionReq)) == -1) {
        perror("write");
        exit(1);
    }

    // wait for server response
    char client_fifo[256];
    snprintf(client_fifo, sizeof(client_fifo), "/tmp/client.%d", getpid());
    if (mkfifo(client_fifo, 0666) == -1) {
        perror("mkfifo");
        exit(1);
    }

    int client_fifo_fd = open(client_fifo, O_RDONLY);
    if (client_fifo_fd == -1) {
        perror("open");
        exit(1);
    }

    connectionRes resp;
    ssize_t bytes =  read(client_fifo_fd, &resp, sizeof(connectionRes));
    if (bytes == sizeof(connectionRes)) {
        if (resp.child_pid == -1)
        {
            printf("Connection request rejected\n");
            exit(0);
        }
        
        printf("Connection established with server %d\n", resp.child_pid);
        char serv_child_fifo[256];
        snprintf(serv_child_fifo, sizeof(serv_child_fifo), "/tmp/server.%d", resp.child_pid);
        if(mkfifo(serv_child_fifo, 0666) == -1) {
            perror("mkfifo");
            exit(1);
        }

        int serv_child_fifo_fd = open(serv_child_fifo, O_WRONLY);
        if (serv_child_fifo_fd == -1) {
            perror("open");
            exit(1);
        }

        printf("disconnect\n");
        // while (1)
        // {
        //  //TODO Client requests   
        // }

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
