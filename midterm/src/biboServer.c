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
#include <time.h>
#include <semaphore.h>
#include <dirent.h>
#include "../include/common.h"

queue_t connection_queue;
char* working_dir;
int max_client_num;
int server_fifo_fd;
char server_fifo[256];
sig_atomic_t* child_num;
sig_atomic_t* client_num;
sig_atomic_t* client_pids;
sem_t *log_mutex;
int log_fd;

void log_message(const char *message) {
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[20];
    strftime(timestamp, 20, "%Y-%m-%d %H:%M:%S", tm_info);
    char log_msg[1024];
    snprintf(log_msg, sizeof(log_msg), "[%s] %s\n", timestamp, message);
    sem_wait(log_mutex);
    write(log_fd, log_msg, strlen(log_msg));
    sem_post(log_mutex);
}

void handle_rejected(pid_t client_pid){
    
    char client_fifo_path[256];
    snprintf(client_fifo_path, sizeof(client_fifo_path), "/tmp/client.%d", client_pid);
    // open client FIFO for writing
    int client_fifo_fd = open(client_fifo_path, O_WRONLY);
    if (client_fifo_fd == -1) {
        perror("open");
        return;
    }
    pid_t child_pid = -1;
    write(client_fifo_fd, &child_pid, sizeof(child_pid));
    close(client_fifo_fd);
}

void handle_client(pid_t client_pid) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return;
    } 
    else if (pid == 0) 
    {   
        (*child_num)++;
        char client_fifo[256];
        pid_t child_pid = getpid();
        int num = (*client_num)++; 
        
        char child_server_fifo[256];
        snprintf(child_server_fifo, sizeof(child_server_fifo), "/tmp/server.%d", child_pid);
        if(mkfifo(child_server_fifo, 0666) == -1){
            perror("mkfifo");
            (*child_num)--;
            exit(1);
        }

        snprintf(client_fifo, sizeof(client_fifo), "/tmp/client.%d", client_pid);

        // open client FIFO for writing
        int client_fifo_fd = open(client_fifo, O_WRONLY);
        if (client_fifo_fd == -1) {
            perror("open client fifo");
            (*child_num)--;
            exit(1);
        }
        
        if (write(client_fifo_fd, &child_pid, sizeof(child_pid)) == -1) {
            perror("write");
            close(client_fifo_fd);
            (*child_num)--;
            exit(1);
        }

        printf("Connection established with client %d as client %d\n", client_pid, num);
        fflush(stdout);
   
        char log_msg[1024];
        snprintf(log_msg, sizeof(log_msg), "Connection established with client %d as client %d\n", client_pid, num);
        log_message(log_msg);

        // open child server FIFO for reading
        int child_server_fifo_fd = open(child_server_fifo, O_RDONLY);
        if (child_server_fifo_fd == -1) {
            perror("open child server fifo");
            close(client_fifo_fd);
            (*child_num)--;
            exit(1);
        }

        while (1)
        {
            Request req;
            int read_bytes = read(child_server_fifo_fd, &req, sizeof(req));
            if(read_bytes == -1) {
                perror("read request");
                close(client_fifo_fd);
                close(child_server_fifo_fd);
                (*child_num)--;
                exit(1);
            }

            switch (req.request)
            {
            case LIST:
                
                break;
            
            case 

            default:
                break;
            }
        }

        close(client_fifo_fd);
        close(child_server_fifo_fd);
        (*child_num)--;
        exit(0);
    }
    else {
        client_pids[(*child_num)-1] = pid;
    }
}

void sigchld_handler(int sig) {

    if(!is_queue_empty(&connection_queue) && (*child_num) < max_client_num) {
        pid_t client_pid = dequeue(&connection_queue);
        handle_client(client_pid);
    }
}

void close_server(){
    free(client_pids);
    munmap(&child_num, sizeof(child_num));
    munmap(&client_num, sizeof(client_num));
    close(server_fifo_fd);
    unlink(server_fifo);
    close(log_fd);
}

void sigint_handler(int sig) {
   
    for(int i = 0; i < (*child_num); i++) {
        kill(client_pids[i], SIGKILL);
    }

    close_server();
    exit(0);
}

void init_server()
{   
    struct stat st;
    if (stat(working_dir, &st) == -1) {
        if (mkdir(working_dir, 0777) == -1) {
            perror("mkdir");
            exit(EXIT_FAILURE);
        }
    }
    else
    {   
        if(!S_ISDIR(st.st_mode)){
            printf("Error: %s is not a directory\n", working_dir);
            exit(EXIT_FAILURE);
        }
    }

    pid_t pid = getpid();
    char log_path[256];
    snprintf(log_path, sizeof(log_path), "/tmp/server%d.log", pid);
    log_fd = open(log_path, O_WRONLY | O_APPEND | O_CREAT, 0666);
    if (log_fd == -1) {
        perror("open_log");
        exit(EXIT_FAILURE);
    }

    log_mutex = mmap(NULL, sizeof(*log_mutex), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sem_init(log_mutex, 1, 1);
    init_queue(&connection_queue);
    child_num = mmap(NULL, sizeof(child_num), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    (*child_num) = 0;
    client_num = mmap(NULL, sizeof(*client_num), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    (*client_num) = 1;
    client_pids = malloc(max_client_num * sizeof(pid_t));

    printf("Server started. Listening for connections...\n");
    
    snprintf(server_fifo, sizeof(server_fifo), "/tmp/server.%d", pid);
    mkfifo(server_fifo, 0666);
    // open server FIFO for reading
    server_fifo_fd = open(server_fifo, O_RDONLY);
    if (server_fifo_fd == -1) {
        perror("open");
        exit(1);
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

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    struct sigaction sa2;
    sa2.sa_handler = sigint_handler;
    sigemptyset(&sa2.sa_mask);
    sa2.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa2, NULL);
    sigaction(SIGTERM, &sa2, NULL);
    sigaction(SIGQUIT, &sa2, NULL);
    sigaction(SIGSTOP, &sa2, NULL);

    working_dir = argv[1];

    init_server();
    
    while (1) {
        connectionReq req;    
        ssize_t num_read = read(server_fifo_fd, &req, sizeof(connectionReq));
        if (num_read == -1) {
            printf("Error: Failed to read connection request from server FIFO: %s\n", strerror(errno));
            continue;
        }

        else if (num_read == 0) //There is no connection request
            continue;
        
        if ((*child_num) == max_client_num) {
            printf("Connection request PID %d ...  Queue is FULL\n", req.client_pid);
            if (req.try_flag == 1)
               handle_rejected(req.client_pid);
            
            else
                enqueue(&connection_queue, req.client_pid);
        } 
        else
            handle_client(req.client_pid);
    }

    close_server();
    return 0;
}