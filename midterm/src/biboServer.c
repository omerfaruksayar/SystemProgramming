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
#include <sys/mman.h>
#include "../include/common.h"

queue_t connection_queue;
char* working_dir;
int max_client_num;
sig_atomic_t child_num;
sig_atomic_t client_num;


void handle_client(pid_t client_pid) {

    pid_t pid = fork();
    if (pid == -1) {
        printf("Error: Failed to fork new process: %s\n", strerror(errno));
        return;
    } 
    else if (pid == 0) 
    {
        char client_fifo_path[256];
        snprintf(client_fifo_path, sizeof(client_fifo_path), "/tmp/client.%d", client_pid);
        // open client FIFO for writing
        int client_fifo_fd = open(client_fifo_path, O_WRONLY);
        if (client_fifo_fd == -1) {
            perror("open");
            exit(1);
        }
        int num = client_num++;
        printf("Connection established with client %d as client %d\n", client_pid, num);
        //send working directory to client
        connectionRes res;
        res.child_pid = getpid();
        res.num = num;
        res.working_dir = working_dir;
        sleep(5);
        write(client_fifo_fd, &res, sizeof(connectionRes));
        close(client_fifo_fd);
        exit(0);
    }
}

void sigchld_handler(int sig) {
    if (child_num != 0)
        child_num--;
    
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

    max_client_num = atoi(argv[2]);
    if (max_client_num <= 0) {
        printf("Error: Invalid max value.\n");
        return 1;
    }

     printf("Server started. Listening for connections...\n");

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL); 

    working_dir = argv[1];
    pid_t pid = getpid();
    char server_fifo[256];
    snprintf(server_fifo, sizeof(server_fifo), "/tmp/server.%d", pid);

    mkfifo(server_fifo, 0666);
    // open server FIFO for reading
    int server_fifo_fd = open(server_fifo, O_RDONLY);
    if (server_fifo_fd == -1) {
        perror("open");
        exit(1);
    }

    init_queue(&connection_queue);
    mmap(&child_num, sizeof(child_num), PROT_READ|PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    child_num = 0;
    mmap(&client_num, sizeof(client_num), PROT_READ|PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    client_num = 1;

    while (1) {
        connectionReq req;    
        ssize_t num_read = read(server_fifo_fd, &req, sizeof(connectionReq));
        if (num_read == -1) {
            printf("Error: Failed to read connection request from server FIFO: %s\n", strerror(errno));
            continue;
        }

        else if (num_read == 0) {
            continue;
        }
        
        if (child_num == max_client_num) {
            // queue request
            enqueue(&connection_queue, req.client_pid);
            printf("Connection request PID %d ...  Queue is FULL\n", req.client_pid);
        } 
        else {
            // handle request
            child_num++;
            handle_client(req.client_pid);
        }
    }
    munmap(&child_num, sizeof(child_num));
    munmap(&client_num, sizeof(client_num));
    close(server_fifo_fd);
    unlink(server_fifo);
    return 0;
}