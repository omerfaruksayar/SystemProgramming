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
    pid_t server_pid = atoi(argv[1]);

    // create connection request struct
    connectionReq req;
    req.pid = getpid();
    if (strcmp(argv[2], "Connect") == 0)
        req.tryFlag = 0; // client will wait in the queue
    
    else if (strcmp(argv[2], "tryConnect") == 0
        req.tryFlag = 1; // client will not wait in the queue


    char server_fifo[256];
    snprintf(server_fifo, MAX_BUF, "/tmp/server.%d", server_pid);
    // send connection request to server
    int server_fifo_fd = open(server_fifo, O_WRONLY);
    if (server_fifo_fd == -1) {
        printf("Error: Failed to open server FIFO: %s\n", strerror(errno));
        exit(1);
    }
    if (write(server_fifo_fd, &req, sizeof(connectionReq), 0) == -1) {
        perror("write");
        close(server_fifo_fd);
        exit(1);
    }

    // wait for server response
    char client_fifo[256];
    snprintf(client_fifo, MAX_BUF, "/tmp/client.%d", getpid());
    if (mkfifo(client_fifo, 0666) == -1) {
        printf("Error: Failed to create client FIFO %s: %s\n", client_fifo, strerror(errno));
        close(server_fifo_fd);
        exit(1);
    }
    int client_fifo_fd = open(client_fifo, O_RDONLY | O_NONBLOCK);
    if (client_fifo_fd == -1) {
        printf("Error: Failed to open client FIFO %s for reading: %s\n", client_fifo, strerror(errno));
        close(server_fifo_fd);
        unlink(client_fifo);
        exit(1);
    }

    connectionResp resp;
    ssize_t num_bytes = read(client_fifo_fd, &resp, sizeof(connectionResp), 0);

    printf("Connection established with server %d\n", resp.child_pid);

    // clean up
    close(server_fifo_fd);
    close(client_fifo_fd);
    unlink(client_fifo_path);

    return 0;
}
