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

size_t copy_file(FILE* src_file, const char* dest_path) {
    FILE* dest_file = fopen(dest_path, "wb");
    if (dest_file == NULL) {
        return 0;
    }

    char buffer[1024];
    size_t bytes_transferred = 0;
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src_file)) > 0) {
        size_t bytes_written = fwrite(buffer, 1, bytes_read, dest_file);
        if (bytes_written != bytes_read) {
            fprintf(stderr, "Error writing to destination file\n");
            fclose(dest_file);
            return bytes_transferred;
        }

        bytes_transferred += bytes_written;
    }

    fclose(dest_file);
    return bytes_transferred;
}

char* get_line(FILE* fp, int line_num) {
    
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

int write_to_line(FILE* file, int line_num, const char* str, const char* path) {
    // Get the current position in the file
    long position = ftell(file);

    // Move the file pointer to the beginning
    rewind(file);

    char buffer[1024];
    int current_line = 1;
    int line_written = 0;

    // Create a temporary file to write the modified contents
    FILE* temp_file = tmpfile();
    if (temp_file == NULL) {
        fprintf(stderr, "Error creating temporary file\n");
        return 0;
    }

    // Read the file line by line and write to the temporary file
    while (fgets(buffer, sizeof(buffer), file)) {
        if (current_line == line_num) {
            // Write the new string to the specified line
            fputs(str, temp_file);
            line_written = 1;
        } else {
            // Write the current line from the original file
            fputs(buffer, temp_file);
        }

        current_line++;
    }

    // If the line was not written (line number was greater than the total lines),
    // return 0 indicating failure
    if (!line_written) {
        fprintf(stderr, "Specified line does not exist\n");
        fclose(temp_file);
        return 0;
    }

    // Move the file pointer back to the original position
    fseek(file, position, SEEK_SET);

    // Close the original file
    fclose(file);

    // Reopen the original file in write mode
    file = fopen(path, "w");
    if (file == NULL) {
        fprintf(stderr, "Error opening file\n");
        fclose(temp_file);
        return 0;
    }

    // Copy the contents from the temporary file to the original file
    rewind(temp_file);
    while (fgets(buffer, sizeof(buffer), temp_file)) {
        fputs(buffer, file);
    }

    fclose(temp_file);
    fclose(file);
    return 1;
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
        int client_fifo_fd = open(client_fifo, O_WRONLY| O_CREAT, 0666);
        if (client_fifo_fd == -1) {
            perror("open client fifo");
            exit(1);
        }
        
        if (write(client_fifo_fd, &child_pid, sizeof(child_pid)) == -1) {
            perror("write");
            close(client_fifo_fd);
            exit(1);
        }

        printf("Connection established with client %d as client %d\n", client_pid, num);
        fflush(stdout);
   
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "Connection established with client %d as client %d\n", client_pid, num);
        log_message(log_msg);

        if(close(client_fifo_fd) == -1){
            perror("close");
            exit(1);
        }

        while (1)
        {   
            // open client FIFO for reading
            int client_fifo_fd1 = open(client_fifo, O_RDONLY| O_CREAT, 0666);
            if (client_fifo_fd1 == -1) {
                perror("open client fifo");
                exit(1);
            }

            Request req;
            int read_bytes = read(client_fifo_fd1, &req, sizeof(req));
            if(read_bytes == -1) {
                perror("read request");
                close(client_fifo_fd1);
                exit(1);
            }

            if(close(client_fifo_fd1) == -1){
                perror("close");
                exit(1);
            }
            
            if (req.request == LIST)
            {   
                char log [256];
                snprintf(log, sizeof(log), "Client %d requested LIST\n", num);
                log_message(log);
                // open client FIFO for writing
                int client_fifo_fd2 = open(client_fifo, O_WRONLY | O_CREAT, 0666);
                if (client_fifo_fd2 == -1) {
                    perror("open client fifo");
                    exit(1);
                }
                DIR* dir = opendir(working_dir);
                if (dir == NULL) {
                    perror("opendir");
                    close(client_fifo_fd);
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
                    write(client_fifo_fd2, entry->d_name, name_len);
                    write(client_fifo_fd2, "\n", 1);
                }
                closedir(dir);
                close(client_fifo_fd2);   
            }
            
            else if(req.request == READF){
                
                // open client FIFO for writing
                int client_fifo_fd2 = open(client_fifo, O_WRONLY| O_CREAT, 0666);
                if (client_fifo_fd2 == -1) {
                    perror("open client fifo");
                    exit(1);
                }

                char log_msg[1024];
                snprintf(log_msg, sizeof(log_msg), "Client %d requested line %ld on %s\n", num, req.offset ,req.filename);
                log_message(log_msg);

                char file_path[512];
                strcpy(file_path, working_dir);
                strcat(file_path, req.filename);
                char* readed;
                FILE* fp = fopen(file_path, "r");
                if (fp == NULL) {
                    char* error_msg = "Invalid file name!";
                    if (write(client_fifo_fd2, error_msg, strlen(error_msg)) == -1) {
                        perror("write");
                        close(client_fifo_fd2);
                        exit(1);
                    }
                }

                else{
                    char semaphore_name[256];
                    strcpy(semaphore_name, "/");
                    strcat(semaphore_name, req.filename);
                    sem_t* semaphore = sem_open(semaphore_name, O_CREAT, 0644, 1);
                    sem_wait(semaphore);
                    readed = get_line(fp, req.offset);
                    sem_post(semaphore);
                    sem_close(semaphore);
                }
                
                if (readed == NULL)
                {
                    char* error_msg = "Invalid line number!";
                    if (write(client_fifo_fd2, error_msg, strlen(error_msg)) == -1) {
                        perror("write");
                        free(readed);
                        close(client_fifo_fd2);
                        exit(1);
                    }                        
                }

                else{
                
                    if(write(client_fifo_fd2, readed, strlen(readed)) == -1){
                        perror("write");
                        close(client_fifo_fd2);
                        (*child_num)--;
                        exit(1);
                    }

                    free(readed);
                }

                if(close(client_fifo_fd2) == -1){
                    perror("close");
                    free(readed);
                    exit(1);
                }
            }

            else if(req.request == WRITET){

                int client_fifo_fd2 = open(client_fifo, O_WRONLY| O_CREAT, 0666);
                if (client_fifo_fd2 == -1) {
                    perror("open client fifo");
                    exit(1);
                }
                char log[8192];
                snprintf(log, sizeof(log), "Client %d requested WRITE %s to %s line %ld\n", num, req.string, req.filename, req.offset);
                log_message(log);
                
                char file_path[512];
                strcpy(file_path, working_dir);
                strcat(file_path, req.filename);

                FILE* fp = fopen(file_path, "r");
                if (fp == NULL) {
                    char* error_msg = "Invalid file name!";
                    if (write(client_fifo_fd2, error_msg, strlen(error_msg)) == -1) {
                        perror("write");
                        close(client_fifo_fd2);
                        exit(1);
                    }
                }

                else
                {   
                    char semaphore_name[256];
                    strcpy(semaphore_name, "/");
                    strcat(semaphore_name, req.filename);
                    sem_t* semaphore = sem_open(semaphore_name, O_CREAT, 0644, 1);
                    sem_wait(semaphore);
                    int i = write_to_line(fp, req.offset, req.string, file_path);
                    sem_post(semaphore);
                    sem_close(semaphore);
                    
                    if (i)
                        write(client_fifo_fd2, "OK", 2);

                    else
                        write(client_fifo_fd2, "Invalid line number!", 21);
                }

                if(close(client_fifo_fd2) == -1){
                    perror("close");
                    exit(1);
                }
            }
            
            else if(req.request == QUIT){
                char log_msg[1024];
                snprintf(log_msg, sizeof(log_msg), "Client %d disconnected\n", num);
                log_message(log_msg);
                printf("%s", log_msg);
                break;
            }

            else if (req.request == KILL)
            {
                char log_msg[1024];
                snprintf(log_msg, sizeof(log_msg), "Client %d requested KILL\n", num);
                log_message(log_msg);
                kill(getpid(), SIGINT);
                break;
            }

            else if(req.request == DOWNLOAD){
                int client_fifo_fd2 = open(client_fifo, O_WRONLY| O_CREAT, 0666);
                if (client_fifo_fd2 == -1) {
                    perror("open client fifo");
                    exit(1);
                }

                char log_msg[1024];
                snprintf(log_msg, sizeof(log_msg), "Client %d requested download file %s\n", num,req.filename);
                log_message(log_msg);

                char file_path[512];
                strcpy(file_path, working_dir);
                strcat(file_path, req.filename);
                char destination[512];
                strcpy(destination, req.client_dir);
                strcat(destination, "/");
                strcat(destination, req.filename);
                size_t transferred;
                FILE* fp = fopen(file_path, "r");
                if (fp == NULL) {
                    char* error_msg = "Invalid file name!\n";
                    if (write(client_fifo_fd2, error_msg, strlen(error_msg)) == -1) {
                        perror("write");
                        close(client_fifo_fd2);
                        exit(1);
                    }
                }

                else{
                    char semaphore_name[256];
                    strcpy(semaphore_name, "/");
                    strcat(semaphore_name, req.filename);
                    sem_t* semaphore = sem_open(semaphore_name, O_CREAT, 0644, 1);
                    sem_wait(semaphore);
                    transferred = copy_file(fp, destination);
                    sem_post(semaphore);
                    sem_close(semaphore);
                }
        
                char message[1024];
                snprintf(message, sizeof(message), "%ld bytes are transferred to client %d\n", transferred, num);
                log_message(message);

                if(write(client_fifo_fd2, message, strlen(message)) == -1){
                    perror("write");
                    close(client_fifo_fd2);
                    (*child_num)--;
                    exit(1);
                }

                if(close(client_fifo_fd2) == -1){
                    perror("close");
                    exit(1);
                }
            }
        
            else if(req.request == UPLOAD){
                int client_fifo_fd2 = open(client_fifo, O_WRONLY| O_CREAT, 0666);
                if (client_fifo_fd2 == -1) {
                    perror("open client fifo");
                    exit(1);
                }

                char log_msg[1024];
                snprintf(log_msg, sizeof(log_msg), "Client %d requested upload file %s\n", num,req.filename);
                log_message(log_msg);

                char file_path[512];
                strcpy(file_path, req.client_dir);
                strcat(file_path, "/");
                strcat(file_path, req.filename);
                char destination[512];
                strcpy(destination, working_dir);
                strcat(destination, req.filename);
                size_t transferred;
                FILE* fp = fopen(file_path, "r");
                if (fp == NULL) {
                    char* error_msg = "Invalid file name!\n";
                    if (write(client_fifo_fd2, error_msg, strlen(error_msg)) == -1) {
                        perror("write");
                        close(client_fifo_fd2);
                        exit(1);
                    }
                }

                else{
                    char semaphore_name[256];
                    strcpy(semaphore_name, "/");
                    strcat(semaphore_name, req.filename);
                    sem_t* semaphore = sem_open(semaphore_name, O_CREAT, 0644, 1);
                    sem_wait(semaphore);
                    transferred = copy_file(fp, destination);
                    sem_post(semaphore);
                    sem_close(semaphore);
                }
        
                char message[1024];
                snprintf(message, sizeof(message), "%ld bytes are transferred to server\n", transferred);
                log_message(message);

                if(write(client_fifo_fd2, message, strlen(message)) == -1){
                    perror("write");
                    close(client_fifo_fd2);
                    (*child_num)--;
                    exit(1);
                }

                if(close(client_fifo_fd2) == -1){
                    perror("close");
                    exit(1);
                }
            }
        }

        exit(0);
    }

    else 
        child_pids[(*child_num)-1] = pid;
    
}

void sigchld_handler(int sig) {
    pid_t pid;
    int status;

    // Wait for any terminated child process
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        (*child_num)--;

    if (!is_queue_empty(&connection_queue) && (*child_num) < max_client_num) {
        pid_t client_pid = dequeue(&connection_queue);
        handle_client(client_pid);
    }
}

void close_server(){
    
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
        char semaphore_name[256];
        strcpy(semaphore_name, "/");
        strcat(semaphore_name, entry->d_name);
        sem_unlink(semaphore_name);
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
  
    char log_msg[1024];
    snprintf(log_msg, sizeof(log_msg), "Server shutting down\n");
    log_message(log_msg);
    
    for (int i = 0; i < (*child_num); i++) {
        kill(child_pids[i], SIGKILL);
    }

    close_server();
    free(child_pids);
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