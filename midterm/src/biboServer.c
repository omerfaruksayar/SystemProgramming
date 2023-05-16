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

int max_client_num;
int server_fifo_fd;
int log_fd;
char* working_dir;
char server_fifo[256];
sig_atomic_t* child_num;
sig_atomic_t* client_num;
sig_atomic_t* child_pids;
queue_t connection_queue;
sem_t *log_mutex;

char* get_line(const char* path, int line_num) {
    FILE* fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error opening file\n");
        exit(EXIT_FAILURE);
    }

    char* line = NULL;
    size_t len = 0;
    ssize_t read;

    int current_line_num = 0;
    while ((read = getline(&line, &len, fp)) != -1) {
        current_line_num++;
        if (current_line_num == line_num) {
            // found the requested line
            fclose(fp);
            return line;
        }
    }

    fclose(fp);
    return NULL;
}

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
    int status = -1;
    write(client_fifo_fd, &status, sizeof(status));
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

        if(close(client_fifo_fd) == -1){
            perror("close");
            (*child_num)--;
            exit(1);
        }

        while (1)
        {   
            // open client FIFO for reading
            client_fifo_fd = open(client_fifo, O_RDONLY);
            if (client_fifo_fd == -1) {
                perror("open client fifo");
                (*child_num)--;
                exit(1);
            }

            Request req;
            int read_bytes = read(client_fifo_fd, &req, sizeof(req));
            if(read_bytes == -1) {
                perror("read request");
                close(client_fifo_fd);
                (*child_num)--;
                exit(1);
            }

            switch (req.request)
            {
                case LIST:
                    if(close(client_fifo_fd) == -1){
                        perror("close");
                        (*child_num)--;
                        exit(1);
                    }

                    // open client FIFO for writing
                    client_fifo_fd = open(client_fifo, O_WRONLY);
                    if (client_fifo_fd == -1) {
                        perror("open client fifo");
                        (*child_num)--;
                        exit(1);
                    }

                    DIR* dir = opendir(working_dir);
                    if (dir == NULL) {
                        perror("opendir");
                        close(client_fifo_fd);
                        (*child_num)--;
                        exit(1);
                    }

                    // Read all directory entries
                    struct dirent* entry;
                    while ((entry = readdir(dir)) != NULL) {
                        // Skip "." and ".." entries
                        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                            continue;
                        }

                        // Write the file name to the FIFO
                        size_t name_len = strlen(entry->d_name);
                        write(client_fifo_fd, entry->d_name, name_len);
                        write(client_fifo_fd, "\n", 1);
                    }
                    closedir(dir);
                    close(client_fifo_fd);
                    break;

                case READF:
                    if(close(client_fifo_fd) == -1){
                        perror("close");
                        (*child_num)--;
                        exit(1);
                    }

                    char log_msg[1024];
                    snprintf(log_msg, sizeof(log_msg), "Client %d requested line %ld on %s\n", num, req.offset ,req.filename);
                    log_message(log_msg);

                    // open client FIFO for writing
                    client_fifo_fd = open(client_fifo, O_WRONLY);
                    if (client_fifo_fd == -1) {
                        perror("open client fifo");
                        (*child_num)--;
                        exit(1);
                    }

                    char file_path[512];
                    strcpy(file_path, working_dir);
                    strcat(file_path, req.filename);
                    
                    sem_t *mutex = sem_open(req.filename, O_CREAT | O_EXCL, 0666, 1);
                    if (errno == EEXIST) {
                        // Semaphore already exists, open it without creation
                        mutex = sem_open(req.filename, 0);
                        if (mutex == SEM_FAILED) {
                            perror("sem_open");
                            exit(EXIT_FAILURE);
                        }
                    } 
                    else {
                        perror("sem_open");
                        exit(EXIT_FAILURE);
                    }
                    sem_wait(mutex);
                    char* readed = get_line(file_path, req.offset);
                    sem_post(mutex);
                    
                    if (readed == NULL)
                    {
                        char* error_msg = "Invalid line number!";
                        if (write(client_fifo_fd, error_msg, strlen(error_msg)) == -1) {
                            perror("write");
                            close(client_fifo_fd);
                            (*child_num)--;
                            exit(1);
                        }                        
                    }

                    else{
                    
                        if(write(client_fifo_fd, readed, strlen(readed)) == -1){
                            perror("write");
                            close(client_fifo_fd);
                            (*child_num)--;
                            free(readed);
                            exit(1);
                        }

                        free(readed);
                    }

                    if(close(client_fifo_fd) == -1){
                        perror("close");
                        (*child_num)--;
                        exit(1);
                    }

                    sem_close(mutex);
                    break;

                default:
                    break;
            }
        }

        close(client_fifo_fd);
        (*child_num)--;
        exit(0);
    }
    else {
        child_pids[(*child_num)-1] = pid;
    }
}

void sigchld_handler(int sig) {

    if(!is_queue_empty(&connection_queue) && (*child_num) < max_client_num) {
        pid_t client_pid = dequeue(&connection_queue);
        handle_client(client_pid);
    }
}

void close_server(){
    free(child_pids);
    
    DIR* dir = opendir(working_dir);
    if (dir == NULL) {
        perror("opendir");
        exit(1);
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        sem_unlink(entry->d_name);
    }
    
    closedir(dir);
    munmap(&child_num, sizeof(child_num));
    munmap(&client_num, sizeof(client_num));
    sem_destroy(log_mutex);
    close(server_fifo_fd);
    unlink(server_fifo);
    close(log_fd);
}

void sigint_handler(int sig) {
   
    for(int i = 0; i < (*child_num); i++) {
        kill(child_pids[i], SIGKILL);
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
    child_num = mmap(NULL, sizeof(child_num), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    (*child_num) = 0;
    client_num = mmap(NULL, sizeof(*client_num), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    (*client_num) = 1;
    child_pids = malloc(max_client_num * sizeof(pid_t));
    init_queue(&connection_queue);
    sem_init(log_mutex, 1, 1);

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
    int dir_len = strlen(working_dir);
    if (working_dir[dir_len-1] != '/'){
        working_dir = strcat(working_dir, "/");
    }

    printf("Working directory: %s\n", working_dir);

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