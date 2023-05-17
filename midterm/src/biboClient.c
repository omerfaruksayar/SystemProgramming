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
int running = 1;

void signal_handler(int sig) {
  
}

int main(int argc, char *argv[]) {
    // parse command-line arguments
    if (argc != 3) {
        printf("Usage: %s <Connect/tryConnect> ServerPID\n", argv[0]);
        exit(1);
    }
    pid_t server_pid = atoi(argv[2]);

    struct sigaction sa2;
    sa2.sa_handler = signal_handler;
    sigemptyset(&sa2.sa_mask);
    sa2.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa2, NULL);
    sigaction(SIGTERM, &sa2, NULL);
    sigaction(SIGQUIT, &sa2, NULL);

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
    if (mkfifo(client_fifo ,0666) == -1) {
        perror("mkfifo client");
        exit(1);
    }

    char server_fifo[256];
    snprintf(server_fifo, sizeof(server_fifo), "/tmp/server.%d", server_pid);
    // send connection request to server
    int server_fifo_fd = open(server_fifo, O_WRONLY);
    if (server_fifo_fd == -1) {
        perror("open server fifo");
        unlink(client_fifo);
        exit(1);
    }
    if (write(server_fifo_fd, &req, sizeof(connectionReq)) == -1) {
        perror("write");
        close(server_fifo_fd);
        unlink(client_fifo);
        exit(1);
    }

    int client_fifo_fd = open(client_fifo, O_RDONLY | O_CREAT, 0666);
    if (client_fifo_fd == -1) {
        perror("open client fifo");
        unlink(client_fifo);
        close(server_fifo_fd);
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
        
        if(close(client_fifo_fd) == -1){
            perror("close");
            close(server_fifo_fd);
            unlink(client_fifo);
            exit(1);
        }

        while(running)
        {   
            // open client fifo for writing
            int client_fifo_fd1 = open(client_fifo, O_WRONLY | O_CREAT, 0666);
            if (client_fifo_fd1 == -1) {
                perror("open client fifo");
                close(server_fifo_fd);
                unlink(client_fifo);
                exit(1);
            }

            Request req;
            printf("Enter a command: ");
            char input[BUFSIZE];
            fgets(input, sizeof(input), stdin);
            fflush(stdin);
            input[strlen(input) - 1] = '\0';
            //parse command according to space character
            char *token = strtok(input, " ");
            if (token == NULL){
                if (close(client_fifo_fd1) == -1)
                {
                    perror("close");
                    close(server_fifo_fd);
                    unlink(client_fifo);
                    exit(1);
                }
                continue;
            }

            char* command = malloc(strlen(token) + 1);
            strcpy(command, token);
            command[strlen(token)] = '\0';

            if (strcmp(command, "help") == 0)
            {   
                token = strtok(NULL, " ");
                if (token != NULL)
                {
                    if (strcmp(token, "readF") == 0)
                        printf("readF <file> <line #>\n");
                    
                    else if (strcmp(token, "writeT") == 0)
                        printf("writeT <file> <line #> <string>\n");
                    
                    else if (strcmp(token, "upload") == 0)
                        printf("upload <file>\n");
                    
                    else if (strcmp(token, "download") == 0)
                        printf("download <file>\n");
                    
                    else
                        printf("No such command!\n");
                }

                else{
                    printf("Possible commands:\n");
                    printf("list\n");
                    printf("readF <file> <line #>\n");
                    printf("writeT <file> <line #> <string>\n");
                    printf("upload <file>\n");
                    printf("download <file>\n");
                    printf("killServer\n");
                    printf("quit\n");   
                }    
                fflush(stdout);
                if (close(client_fifo_fd1) == -1)
                {
                    perror("close");
                    close(server_fifo_fd);
                    unlink(client_fifo);
                    exit(1);
                }
            }

            else if(strcmp(command,"list") == 0){
                req.request = LIST;
                if (write(client_fifo_fd1, &req, sizeof(Request)) == -1) {
                    perror("write");
                    close(server_fifo_fd);
                    close(client_fifo_fd1);
                    unlink(client_fifo);
                    free(command);
                    exit(1);
                }

                if(close(client_fifo_fd1) == -1){
                    perror("close");
                    close(server_fifo_fd);
                    unlink(client_fifo);
                    free(command);
                    exit(1);
                }

                int client_fifo_fd2 = open(client_fifo, O_RDONLY | O_CREAT, 0666);
                if (client_fifo_fd2 == -1) {
                    perror("open client fifo");
                    close(server_fifo_fd);
                    unlink(client_fifo);
                    free(command);
                    exit(1);
                }

                char buf[256];
                memset(buf, 0, sizeof(buf));

                while (read(client_fifo_fd2, &buf, sizeof(buf)) != 0)
                {
                    printf("%s", buf);
                    memset(buf, 0, sizeof(buf));
                }

                if (close(client_fifo_fd2) == -1)
                {
                    perror("close");
                    close(server_fifo_fd);
                    unlink(client_fifo);
                    exit(1);
                }
            }

            else if(strcmp(command,"readF") == 0){

                char* filename = strtok(NULL, " ");
                if (filename == NULL)
                {
                    printf("Usage: readF <file> <line #>\n");
                    free(command);
                    if (close(client_fifo_fd1) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                    continue;
                }

                char* lineNum = strtok(NULL, " ");
                if (lineNum == NULL)
                {
                    printf("Usage: readF <file> <line #>\n");
                    free(command);
                    if (close(client_fifo_fd1) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                    continue;
                }

                req.request = READF;
                strcpy(req.filename, filename);
                req.filename[strlen(filename)] = '\0';
                req.offset = atoi(lineNum);

                if (write(client_fifo_fd1, &req, sizeof(Request)) == -1) {
                    perror("write");
                    free(command);
                    if (close(client_fifo_fd1) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                    continue;
                }

                if(close(client_fifo_fd1) == -1){
                    perror("close");
                    free(command);
                    if (close(client_fifo_fd1) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                    exit(1);
                }

                int client_fifo_fd2 = open(client_fifo, O_RDONLY | O_CREAT, 0666);
                if (client_fifo_fd2 == -1) {
                    perror("open client fifo");
                    free(command);
                    if (close(client_fifo_fd2) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                    continue;
                }

                char buf[BUFSIZE];
                memset(buf, 0, sizeof(buf));

                while (read(client_fifo_fd2, &buf, sizeof(buf)) != 0)
                {
                    printf("%s", buf);
                    memset(buf, 0, sizeof(buf));
                }
                printf("\n");

                if (close(client_fifo_fd2) == -1)
                {
                    perror("close");
                    close(server_fifo_fd);
                    unlink(client_fifo);
                    exit(1);
                }
            }

            else if(strcmp(command, "writeT") == 0)
            {
                char* filename = strtok(NULL, " ");
                if (filename == NULL)
                {
                    printf("Usage: writeT <file> <line #> <string>\n");
                    free(command);
                    if (close(client_fifo_fd1) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                    continue;
                }

                char* lineNum = strtok(NULL, " ");
                if (lineNum == NULL)
                {
                    printf("Usage: writeT <file> <line #> <string>\n");
                    free(command);
                    if (close(client_fifo_fd1) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                    continue;
                }

                char* string = strtok(NULL, " ");
                if (string == NULL)
                {
                    printf("Usage: writeT <file> <line #> <string>\n");
                    free(command);
                    if (close(client_fifo_fd1) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                    continue;
                }

                req.request = WRITET;
                strcpy(req.filename, filename);
                req.filename[strlen(filename)] = '\0';
                req.offset = atoi(lineNum);
                
                strcpy(req.string, string);
                req.string[strlen(string)] = '\0';

                if (write(client_fifo_fd1, &req, sizeof(Request)) == -1) {
                    perror("write");
                    free(command);
                    if (close(client_fifo_fd1) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                    continue;
                }

                if(close(client_fifo_fd1) == -1){
                    perror("close");
                    free(command);
                    if (close(client_fifo_fd1) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                    continue;
                }

                int client_fifo_fd2 = open(client_fifo, O_RDONLY| O_CREAT, 0666);
                if (client_fifo_fd2 == -1) {
                    perror("open client fifo");
                    free(command);
                    if (close(client_fifo_fd2) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                    continue;
                }

                char buf[BUFSIZE];
                memset(buf, 0, sizeof(buf));

                while(read(client_fifo_fd2, &buf, sizeof(buf)) != 0)
                {
                    printf("%s", buf);
                    memset(buf, 0, sizeof(buf));
                }

        
                printf("\n");
                if (close(client_fifo_fd2) == -1)
                {
                    perror("close");
                    close(server_fifo_fd);
                    unlink(client_fifo);
                    exit(1);
                }
            }

            else if(strcmp(command, "quit") == 0)
            {
                printf("Bye\n");
                req.request = QUIT;
                if(write(client_fifo_fd1, &req, sizeof(Request)) == -1)
                {
                    perror("write");
                    free(command);
                    if (close(client_fifo_fd1) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                }
                running = 0;
                if (close(client_fifo_fd1) == -1)
                {
                    perror("close");
                    close(server_fifo_fd);
                    unlink(client_fifo);
                    exit(1);
                }
            }

            else if(strcmp(command, "killServer") == 0){
                printf("Bye\n");
                req.request = KILL;
                if(write(client_fifo_fd1, &req, sizeof(Request)) == -1)
                {
                    perror("write");
                    free(command);
                    if (close(client_fifo_fd1) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                }
                running = 0;
                if (close(client_fifo_fd1) == -1)
                {
                    perror("close");
                    close(server_fifo_fd);
                    unlink(client_fifo);
                    exit(1);
                }
            }

            else if(strcmp(command, "download") == 0){
                char* file = strtok(NULL, " ");
                if (file == NULL)
                {
                    printf("Usage: download <file>\n");
                    free(command);
                    if (close(client_fifo_fd1) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                    continue;
                }

                req.request = DOWNLOAD;
                strcpy(req.filename, file);
                req.filename[strlen(file)] = '\0';
                if (getcwd(req.client_dir, sizeof(req.client_dir)) == NULL) {
                    perror("getcwd");
                    close(server_fifo_fd);
                    unlink(client_fifo);
                    exit(1);
                }
                
                if(write(client_fifo_fd1, &req, sizeof(Request)) == -1)
                {
                    perror("write");
                    free(command);
                    if (close(client_fifo_fd1) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                }

                if(close(client_fifo_fd1) == -1){
                    perror("close");
                    free(command);
                    if (close(client_fifo_fd1) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                }

                int client_fifo_fd2 = open(client_fifo, O_RDONLY| O_CREAT, 0666);
                if (client_fifo_fd2 == -1) {
                    perror("open client fifo");
                    free(command);
                    if (close(client_fifo_fd2) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                }

                char buf[BUFSIZE];
                memset(buf, 0, sizeof(buf));

                while(read(client_fifo_fd2, &buf, sizeof(buf)) != 0)
                {
                    printf("%s", buf);
                    memset(buf, 0, sizeof(buf));
                }
            }
            
            else if(strcmp(command, "upload")== 0){
                char* file = strtok(NULL, " ");
                if (file == NULL)
                {
                    printf("Usage: upload <file>\n");
                    free(command);
                    if (close(client_fifo_fd1) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                    continue;
                }

                req.request = UPLOAD;
                strcpy(req.filename, file);
                req.filename[strlen(file)] = '\0';
                if (getcwd(req.client_dir, sizeof(req.client_dir)) == NULL) {
                    perror("getcwd");
                    close(server_fifo_fd);
                    unlink(client_fifo);
                    exit(1);
                }
                
                if(write(client_fifo_fd1, &req, sizeof(Request)) == -1)
                {
                    perror("write");
                    free(command);
                    if (close(client_fifo_fd1) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                }

                if(close(client_fifo_fd1) == -1){
                    perror("close");
                    free(command);
                    if (close(client_fifo_fd1) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                }

                int client_fifo_fd2 = open(client_fifo, O_RDONLY| O_CREAT, 0666);
                if (client_fifo_fd2 == -1) {
                    perror("open client fifo");
                    free(command);
                    if (close(client_fifo_fd2) == -1)
                    {
                        perror("close");
                        close(server_fifo_fd);
                        unlink(client_fifo);
                        exit(1);
                    }
                }

                char buf[BUFSIZE];
                memset(buf, 0, sizeof(buf));

                while(read(client_fifo_fd2, &buf, sizeof(buf)) != 0)
                {
                    printf("%s", buf);
                    memset(buf, 0, sizeof(buf));
                }
            }
            
            else
                printf("Invalid command!\n");

            free(command);
        }
    }

    else 
    {   
        perror("read");
        close(server_fifo_fd);
        close(client_fifo_fd);
        unlink(client_fifo);
        exit(1); 
    }

    // clean up
    close(server_fifo_fd);
    close(client_fifo_fd);
    unlink(client_fifo);

    return 0;
}