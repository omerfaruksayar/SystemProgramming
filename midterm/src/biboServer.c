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

volatile sig_atomic_t children_num = 0;
volatile sig_atomic_t client_num = 0;

queue_t connection_queue;
char* working_dir;

void handle_client(pid_t client_pid) {
    
    children_num++;
    pid_t pid = fork();
    if (pid == -1) {
        printf("Error: Failed to fork new process: %s\n", strerror(errno));
        return;
    } 
    else if (pid == 0) 
    {
        // child process
        // construct client FIFO path
        char client_fifo_path[256];
        snprintf(client_fifo_path, sizeof(client_fifo_path), "/tmp/client.%d", client_pid);
        // open client FIFO for writing
        int client_fifo_fd = open(client_fifo_path, O_WRONLY);
        if (client_fifo_fd == -1) {
            printf("Error: Failed to open client FIFO %s for writing: %s\n", client_fifo_path, strerror(errno));
            exit(1);
        }
        int num = client_num++;
        printf("Connection established with client %d as client %d\n", client_pid, num);
        //send working directory to client
        connectionRes res;
        res.child_pid = getpid();
        res.num = num;
        res.working_dir = working_dir;
        write(client_fifo_fd, &res, sizeof(connectionRes));
        exit(0);
    }
}

void sigchld_handler(int sig) {
    children_num--;
    // check if there are queued requests
    if(!is_queue_empty(&connection_queue)) {
        pid_t client_pid = dequeue(&connection_queue);
        handle_client(client_pid);
    }
}

int main(int argc, char* argv[]) {
    if(argc < 3)
    {
        printf("Usage: %s <dirname> <max. #ofClients>\n", argv[0]);
        return 1;
    }

    int max = atoi(argv[2]);
    if (max <= 0) {
        printf("Error: Invalid max value.\n");
        exit(1);
    }

    working_dir = argv[1];

    mkfifo(SERVER_FIFO_PATH, 0666);
    // open server FIFO for reading
    int server_fifo_fd = open(SERVER_FIFO_PATH, O_RDONLY);
    if (server_fifo_fd == -1) {
        printf("Error: Failed to open server FIFO %s for reading: %s\n", SERVER_FIFO_PATH, strerror(errno));
        exit(1);
    }

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL); 
        
    printf("Server started. Listening for connections...\n");    
    
    while (1) {
        connectionReq req;
        ssize_t num_read = read(server_fifo_fd, &req, sizeof(connectionReq));
        if (num_read == -1) {
            printf("Error: Failed to read connection request from server FIFO: %s\n", strerror(errno));
            continue;
        } 
        else if (num_read == 0) {
            printf("Server FIFO closed by client.\n");
            break;
        }
        
        if (children_num >= max) {
            // queue request
            enqueue(&connection_queue, req.client_pid);
            printf("Client %d queued due to maximum number of child processes reached.\n", req.client_pid);
        } 
        else {
            // handle request immediately
            handle_client(req.client_pid, 0);
        }
    }
    close(server_fifo_fd);
    unlink(SERVER_FIFO_PATH);
    return 0;
}