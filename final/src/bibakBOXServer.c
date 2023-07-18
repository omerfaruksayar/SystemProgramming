#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <limits.h>
#include <dirent.h>
#include <netdb.h>
#include <fcntl.h>
#include "../include/common.h"

pthread_mutex_t serverMutex;                            // Mutex for server directory updates
pthread_cond_t serverCond;                              // Condition variable to signal new client connection
pthread_t* threads;                                     // Thread IDs of the server threads
int threadPoolSize = 0;                                 // Number of server threads
int port = 0;                                           // Port number of the server
int logFD = 0;
int threadCount = 0;                                    // Number of active server threads
char* directory = NULL;                                 // Directory to be shared by the server
volatile sig_atomic_t shutdownServer = 0;               // Flag to indicate server shutdown
int clientSocketFD = 0;                                 // Temporary client socket file descriptor
size_t max_entries = 8192;                              // Max number of entries in the directory (It is dynamically increased if needed)

void signalHandler(int signum) {
    printf("Shutting down server...\n");
    shutdownServer = 1;
    pthread_cond_broadcast(&serverCond);
}

int create_directory(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode))
            return 0;  // Directory already exists
        else
            return -1;  // Path exists, but it's not a directory
    }

    // Create directory
    if (mkdir(path, 0777) == -1)
        return -1;  // Failed to create directory

    return 0;
}

void readDirectory(const char* path, Info* entries, size_t* entryCount) {
    DIR* dir = opendir(path);
    if (dir == NULL) {
        perror("Failed to open directory");
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "log.txt") == 0) {
            continue;
        }

        char* entryPath = (char*)malloc(MAX_PATH_LENGTH);
        
        strcpy(entryPath, path);
        strcat(entryPath, "/");
        strcat(entryPath, entry->d_name);

        char* tmp = (char*)malloc(MAX_PATH_LENGTH);
        memset(tmp, 0, MAX_PATH_LENGTH);
        int j = 0;
        for (size_t i = strlen(directory); i < strlen(entryPath); i++)
        {
            tmp[j++] = entryPath[i]; 
        }
        
        Info info;
        info.type = entry->d_type;
        info.isDeleted = 0;
        strcpy(info.path, entryPath);
        strcpy(info.name, tmp);
        struct stat st;
        if (stat(entryPath, &st) == 0) {
            info.size = st.st_size;
            info.lastModified = st.st_mtime;
        }

        free(tmp);

        if (*entryCount == max_entries) {
            max_entries *= 2;
            entries = realloc(entries, max_entries * sizeof(Info));
            if (entries == NULL) {
                perror("Failed to allocate memory");
                exit(1);
            }
        }

        entries[(*entryCount)++] = info;

        if (entry->d_type == DT_DIR) {
            readDirectory(entryPath, entries, entryCount);
        }

        free(entryPath);
    }

    closedir(dir);
}

void log_message(const char *message) {
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[20];
    strftime(timestamp, 20, "%Y-%m-%d %H:%M:%S", tm_info);
    char log_msg[1024];
    snprintf(log_msg, sizeof(log_msg), "[%s] %s\n", timestamp, message);
    write(logFD, log_msg, strlen(log_msg));
   
}

void sendFile(int socketFD, const Info* info) {
    char path[MAX_PATH_LENGTH];
    strcpy(path, directory);
    strcat(path, info->path);
    FILE* file = fopen(info->path, "rb");
    if (file == NULL) {
        perror("Failed to open file");
        exit(1);
    }

    char buffer[4096];
    size_t bytesRead;
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send(socketFD, buffer, bytesRead,0) == -1) {
            perror("Failed to send file data to client");
            exit(1);
        }
    }

    fclose(file);
}

void* workerThread(void* arg) {
    
    while(!shutdownServer){

        pthread_mutex_lock(&serverMutex);
        pthread_cond_wait(&serverCond, &serverMutex);

        if (shutdownServer) {
            pthread_mutex_unlock(&serverMutex);
            threadCount--;
            pthread_exit(NULL);
        }

        int clientSocket = clientSocketFD;
        pthread_mutex_unlock(&serverMutex);

        int cs = 1;
        send(clientSocket, &cs,sizeof(int),0);
        ResponseType clientResponse;
       
        Info* entries = (Info*)malloc(max_entries * sizeof(Info));
        memset(entries, 0, max_entries * sizeof(Info));
        size_t entryCount = 0;
        size_t prevEntryCount = 0;

        while(cs && !shutdownServer){
            
        /*---------------------------------------------------UPDATE CLIENT-----------------------------------------------------*/

            //Previous State
            pthread_mutex_lock(&serverMutex);
            Info* dir = (Info*)malloc(entryCount * sizeof(Info));
            memset(dir, 0, entryCount * sizeof(Info));
            memcpy(dir, entries, entryCount * sizeof(Info));
            prevEntryCount = entryCount;
            pthread_mutex_unlock(&serverMutex);

            sleep(1);
            //Current State
            pthread_mutex_lock(&serverMutex);
            entryCount = 0;
            memset(entries, 0, max_entries * sizeof(Info));
            readDirectory(directory, entries, &entryCount);

            Info* diffArray = (Info*)malloc(max_entries * sizeof(Info));
            memset(diffArray, 0, max_entries * sizeof(Info));
            size_t diffCount = 0;

            //find only deleted files
            for (size_t i = 0; i < prevEntryCount; i++) {
                Info prevInfo = dir[i];
                int found = 0;
                for (size_t j = 0; j < entryCount; j++) {
                    Info info = entries[j];
                    if (strcmp(prevInfo.path, info.path) == 0) {
                        found = 1;
                        break;
                    }
                }

                if (!found) {
                    prevInfo.isDeleted = 1;
                    diffArray[diffCount++] = prevInfo;
                }
            }

            //find new and modified files
            for (size_t i = 0; i < entryCount; i++) {
                Info info = entries[i];
                int found = 0;
                for (size_t j = 0; j < prevEntryCount; j++) {
                    Info prevInfo = dir[j];
                    if (strcmp(info.path, prevInfo.path) == 0) {
                        found = 1;
                        if (info.lastModified != prevInfo.lastModified && info.type == DT_REG) {
                            diffArray[diffCount++] = info;
                        }
                        break;
                    }
                }

                if (!found) {
                    diffArray[diffCount++] = info;
                }
            }
 
            pthread_mutex_unlock(&serverMutex);

            if (send(clientSocket, &diffCount, sizeof(size_t),0) == -1) {
                break;
            }

            if (recv(clientSocket, &clientResponse, sizeof(ResponseType),0) <= 0){
                break;
            }
            
            if(clientResponse == ERROR){
                continue;
            }

            pthread_mutex_lock(&serverMutex);

            printf("Sending %zu entries to client.\n", diffCount);

            for (size_t i = 0; i < diffCount; i++) {
                Info info = diffArray[i];
                printf("Synchronize '%s' to client.\n", info.path);
                if (send(clientSocket, &info, sizeof(Info),0) == -1) {
                    perror("Failed to send info to client");
                    continue;
                }

                if (info.type == DT_DIR) {
                    
                    if (recv(clientSocket, &clientResponse, sizeof(ResponseType),0) == -1) {
                        perror("Failed to read directory creation from client");
                        continue;
                    }
                    if (clientResponse == OK) {

                        if (info.isDeleted)
                            printf("Directory '%s' deleted from the client.\n", info.name);
                        
                        else
                            printf("Directory '%s' created on the client.\n", info.name);
                    }
                    
                } 
                else if (info.type == DT_REG) {
                    // Send the file data
                    recv(clientSocket, &clientResponse, sizeof(ResponseType),0);
                    if(clientResponse == ERROR){
                        continue;
                    }

                    if (info.isDeleted)
                    {   
                        char log[256];
                        strcpy(log, "File '");
                        strcat(log, info.name);
                        strcat(log, "' deleted from the client.");
                        log_message(log);
                        continue;
                    }

                    if (info.size == 0)
                    {   
                        continue;
                    }
                    
                    sendFile(clientSocket, &info);
                    char log[MAX_PATH_LENGTH];
                    strcpy(log, "File ");
                    strcat(log, info.name);
                    strcat(log, " last modified at ");
                    strcat(log, ctime(&info.lastModified));
                    log_message(log);

                    // Wait for client to receive the file
                    if (recv(clientSocket, &clientResponse, sizeof(ResponseType),0) == -1) {
                        perror("Failed to read file response from client");
                        continue;
                    }
                }
            }
            
            pthread_mutex_unlock(&serverMutex);
            
            free(dir);
            free(diffArray);

        /*---------------------------------------------------UPDATE DIR-----------------------------------------------------*/
            if (send(clientSocket, &clientResponse, sizeof(ResponseType),0) == -1)
            {   
                perror("Failed to send update response");
                break;
            }

            size_t entryNum = 0;
            ResponseType response;
            struct stat statbuf;

            if (recv(clientSocket, &entryNum, sizeof(size_t),0) <= 0) {
                cs = 0;
                break;
            }

            printf("Number of entries coming: %ld\n", entryNum);
            
            response = OK;
            pthread_mutex_lock(&serverMutex);
            if (send(clientSocket, &response, sizeof(ResponseType), 0) == -1) {
                perror("Failed to send response to client");
                cs = 0;
                break;
            }
            
            for (size_t i = 0; i < entryNum; i++){   
                Info info;
                if(recv(clientSocket, &info, sizeof(Info),0) <= 0){
                    response = ERROR;
                    if(send(clientSocket, &response, sizeof(ResponseType), 0) == -1){
                        perror("Failed to send response to client");
                        cs = 0;
                        break;
                    }
                    response = OK;
                    continue;
                }
            
                char path[MAX_PATH_LENGTH];
                strcpy(path, directory);
                strcat(path, info.name);
                
                if (info.type == DT_DIR)
                {   
                    if (info.isDeleted){
                        char cmd[MAX_PATH_LENGTH];
                        strcpy(cmd, "rm -rf ");
                        strcat(cmd, path);
                        system(cmd);     
                    }
                        
                    else{
                        mkdir(path,0777);
                        struct utimbuf new_times;
                        stat(path, &statbuf);
                        new_times.actime = statbuf.st_atime;  // Keep the current last access time
                        new_times.modtime = info.lastModified;    // Set the new modified time
                        utime(path, &new_times);
                    }

                    if(send(clientSocket, &response, sizeof(ResponseType), 0) == -1){
                        perror("Failed to send response to server");
                        cs = 0;
                        break;
                    }
                }
                
                else if (info.type == DT_REG)
                {   
                    if (info.isDeleted == 1)
                    {
                        remove(path);
                        char log[256];
                        strcpy(log, "File '");
                        strcat(log, info.name);
                        strcat(log, "' deleted from the server.");
                        log_message(log);

                        if(send(clientSocket, &response, sizeof(ResponseType), 0) == -1){
                            perror("Failed to send response to server");
                            cs = 0;
                            break;
                        }
                        continue;
                    }

                    if (stat(path, &statbuf) == 0)
                    {
                        if (statbuf.st_mtime == info.lastModified)
                        {
                            response = ERROR;
                            if(send(clientSocket, &response, sizeof(ResponseType), 0) == -1){
                                perror("Failed to send response to server");
                                cs = 0;
                                break;
                            }
                            continue;
                        }
                    }
                    
                    //read socket until the info.size reached and write to file
                    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
                    if(send(clientSocket, &response, sizeof(ResponseType), 0) == -1){
                        perror("Failed to send response to server");
                        cs = 0;
                        break;
                    }
                    
                    if (info.size > 4096){
                        int chunk = info.size / 4096;
                        int remainder = info.size % 4096;
                        for (int i = 0; i < chunk; i++)
                        {
                            char* buffer = (char*)malloc(4096);
                            recv(clientSocket, buffer, 4096, 0);
                            write(fd, buffer, 4096);
                            free(buffer);
                        }
                        char* buffer = (char*)malloc(remainder);
                        recv(clientSocket, buffer, remainder, 0);
                        write(fd, buffer, remainder);
                        free(buffer);
                    }

                    else if(info.size < 4096 && info.size > 0){
                        char* buffer = (char*)malloc(info.size);
                        recv(clientSocket, buffer, info.size, 0);
                        write(fd, buffer, info.size);
                        free(buffer);
                    }

                    else{
                        close(fd);
                        continue;
                    }

                    struct utimbuf new_times;
                    stat(path, &statbuf);
                    new_times.actime = statbuf.st_atime;  // Keep the current last access time
                    new_times.modtime = info.lastModified;    // Set the new modified time
                    utime(path, &new_times);

                    char log[MAX_PATH_LENGTH];
                    strcpy(log, "File ");
                    strcat(log, info.name);
                    strcat(log, " last modified at ");
                    strcat(log, ctime(&info.lastModified));
                    log_message(log);

                    if(send(clientSocket, &response, sizeof(ResponseType), 0) == -1){
                        perror("Failed to send response to server");
                        cs = 0;
                        close(fd);
                        break;
                    }
                    close(fd);
                }
            }

            pthread_mutex_unlock(&serverMutex);
        }

        printf("Client disconnected!\n");
        free(entries);    
        threadCount--;
        close(clientSocket);
    }
    return NULL;
}

void initializeServer() {
    int serverSock, clientSock;
    struct sockaddr_in serverAddress, clientAddress;
    socklen_t clientAddressLength = sizeof(clientAddress);
    threads = (pthread_t*)malloc(threadPoolSize * sizeof(pthread_t));

    // Create server socket
    serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        perror("Failed to create server socket");
        exit(1);
    }

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        perror("Failed to get hostname");
        close(serverSock);
        exit(1);
    }
    struct addrinfo* serverInfo;
    if (getaddrinfo(hostname, NULL, NULL, &serverInfo) != 0) {
        perror("Failed to get address information");
        close(serverSock);
        exit(1);
    }

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port);

    if (bind(serverSock, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
        perror("Failed to bind server socket");
        close(serverSock);
        freeaddrinfo(serverInfo);
        exit(1);
    }

    // Listen for incoming connections
    if (listen(serverSock, threadPoolSize) == -1) {
        perror("Failed to listen for connections");
        close(serverSock);
        freeaddrinfo(serverInfo);
        exit(1);
    }

    // Initialize the mutex and condition variable
    pthread_mutex_init(&serverMutex, NULL);
    pthread_cond_init(&serverCond, NULL);

    create_directory(directory);
    const char* log = "/log.txt";
    char logPath[512];
    strcpy(logPath,directory);
    strcat(logPath,log);

    logFD = open(logPath,O_WRONLY | O_CREAT | O_APPEND, 0666);

    // Create worker threads
    for (int i = 0; i < threadPoolSize; i++) {
        if (pthread_create(&threads[i], NULL, workerThread, NULL) != 0) {
            perror("Failed to create server worker thread");
            break;
        }
    }
    
    printf("Server started successfully. Listening on %s:%d\n", inet_ntoa(serverAddress.sin_addr), port);

    while (!shutdownServer) {
        
        clientSock = accept(serverSock, (struct sockaddr*)&clientAddress, &clientAddressLength);

        if (threadPoolSize == threadCount)
        {
            printf("Thread Pool is full. Connection rejected from %s:%d\n", inet_ntoa(clientAddress.sin_addr), ntohs(clientAddress.sin_port));
            int cs = 0;
            send(clientSock, &cs, sizeof(int),0);
            close(clientSock);
            continue;
        }
        
        pthread_mutex_lock(&serverMutex);
        clientSocketFD = clientSock;
        printf("Client connected from %s:%d\n", inet_ntoa(clientAddress.sin_addr), ntohs(clientAddress.sin_port));
        fflush(stdout);
        threadCount++;
        pthread_cond_signal(&serverCond);
        pthread_mutex_unlock(&serverMutex);
    }

    for (int i = 0; i < threadPoolSize; i++) {
        pthread_join(threads[i], NULL);
    }

    // Clean up
    close(serverSock);
    close(clientSock);
    close(logFD);
    freeaddrinfo(serverInfo);
    free(threads);
    pthread_mutex_destroy(&serverMutex);
    pthread_cond_destroy(&serverCond);
}

int main(int argc, char* argv[]) {
    // Check the number of command line arguments
    if (argc < 4) {
        printf("Usage: %s [directory] [threadPoolSize] [portnumber]\n", argv[0]);
        return 1;
    }

    // Parse command line arguments
    directory = argv[1];
    if (directory[strlen(directory)-1] == '/')
        directory[strlen(directory)-1] = '\0';
    
    threadPoolSize = atoi(argv[2]);
    port = atoi(argv[3]);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signalHandler;
    sigaction(SIGINT, &sa, NULL);

    // Initialize the server
    initializeServer();

    return 0;
}