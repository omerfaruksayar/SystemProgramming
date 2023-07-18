#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <limits.h>
#include <dirent.h>
#include <netdb.h>
#include <utime.h>
#include <fcntl.h>
#include "../include/common.h"

char* directory = NULL;          // Directory to be shared by the client
size_t max_entries = 8192;       // 
int socketFD = 0;
int port = 0;
int shutDown = 0;
char* serverAddress = NULL;
Info* entries = NULL;
size_t entryCount = 0;

void signalHandler(int signum) {
    printf("Shutting down client...\n");
    shutDown = 1;
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
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
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

void synchronizeDirectories() {

    /*--------------------------------------- UPDATE DIR -------------------------------------------*/

    while (!shutDown)
    {      
        struct stat statbuf;
        ResponseType response;
        size_t prevEntryCount; 
        size_t entryNum = 0;

        if (recv(socketFD, &entryNum, sizeof(size_t),0) == -1) {
            perror("Failed to read entry number from server");
            break;
        }

        printf("Number of entries coming: %ld\n", entryNum);
        fflush(stdout);

        response = OK;
        if (send(socketFD, &response, sizeof(ResponseType), 0) == -1) {
            perror("Failed to send response to server");
            break;
        }

        for (size_t i = 0; i < entryNum; i++)
        {   
            Info info;
            if(recv(socketFD, &info, sizeof(Info),0) == -1){
                response = ERROR;
                if(send(socketFD, &response, sizeof(ResponseType), 0) == -1){
                    perror("Failed to send response to server");
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

                if(send(socketFD, &response, sizeof(ResponseType), 0) == -1){
                    perror("Failed to send response to server");
                    break;
                }
            }
            
            else if (info.type == DT_REG)
            {   
                if (info.isDeleted == 1)
                {
                    remove(path);
                    if(send(socketFD, &response, sizeof(ResponseType), 0) == -1){
                        perror("Failed to send response to server");
                        break;
                    }
                    continue;
                }

                if (stat(path, &statbuf) == 0)
                {
                    if (statbuf.st_mtime == info.lastModified)
                    {
                        response = ERROR;
                        if(send(socketFD, &response, sizeof(ResponseType), 0) == -1){
                            perror("Failed to send response to server");
                            break;
                        }
                        continue;
                    }
                }
                
                //read socket until the info.size reached and write to file
                int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
                if(send(socketFD, &response, sizeof(ResponseType), 0) == -1){
                    perror("Failed to send response to server");
                    break;
                }
                
                if (info.size > 4096){
                    int chunk = info.size / 4096;
                    int remainder = info.size % 4096;
                    for (int i = 0; i < chunk; i++)
                    {
                        char* buffer = (char*)malloc(4096);
                        recv(socketFD, buffer, 4096, 0);
                        write(fd, buffer, 4096);
                        free(buffer);
                    }
                    char* buffer = (char*)malloc(remainder);
                    recv(socketFD, buffer, remainder, 0);
                    write(fd, buffer, remainder);
                    free(buffer);
                }

                else if(info.size < 4096 && info.size > 0){
                    char* buffer = (char*)malloc(info.size);
                    recv(socketFD, buffer, info.size, 0);
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

                if(send(socketFD, &response, sizeof(ResponseType), 0) == -1){
                    perror("Failed to send response to server");
                    close(fd);
                    break;
                }
                close(fd);
            }
        }
        
    /*------------------------------------ UPDATE SERVER -------------------------------------------*/

        if(recv(socketFD, &response, sizeof(ResponseType),0) == -1){
            perror("Failed to receive update response");
            break;
        }

        Info* dir = (Info*)malloc(entryCount * sizeof(Info));
        memset(dir, 0, entryCount * sizeof(Info));
        memcpy(dir, entries, entryCount * sizeof(Info));
        prevEntryCount = entryCount;
        sleep(1);

        entryCount = 0;
        memset(entries, 0, max_entries * sizeof(Info));
        readDirectory(directory, entries, &entryCount);
        
        Info* diffArray = (Info*)malloc(max_entries * sizeof(Info));
        memset(diffArray, 0, max_entries * sizeof(Info));
        size_t diffCount = 0;

        // find only deleted files
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
        
        if (send(socketFD, &diffCount, sizeof(size_t),0) == -1) {
            perror("Failed to send diff count to client");
            free(dir);
            free(diffArray);
            break;
        }

        if(recv(socketFD, &response, sizeof(ResponseType),0) == -1){
            perror("Failed to read diff response from client");
            free(dir);
            free(diffArray);
            break;

        }

        if(response == ERROR){
            continue;
        }

        printf("Sending %zu entries to server.\n", diffCount);
        fflush(stdout);

        for (size_t i = 0; i < diffCount; i++) {
            Info info = diffArray[i];
            printf("Synchronize '%s' to client.\n", info.path);
            if (send(socketFD, &info, sizeof(Info),0) == -1) {
                perror("Failed to send info to client");
                continue;
            }

            if (info.type == DT_DIR) {
                
               if (recv(socketFD, &response, sizeof(ResponseType),0) == -1) {
                   perror("Failed to read directory creation response from client");
                   continue;
               }
               if (response == OK) {

                    if (info.isDeleted)
                        printf("Directory '%s' deleted from the client.\n", info.name);
                    
                    else
                        printf("Directory '%s' created on the client.\n", info.name);
                }
                else 
                    printf("Failed to synchronize directory '%s' on the client.\n", info.name);
            } 
            else if (info.type == DT_REG) {
                // Send the file data
                recv(socketFD, &response, sizeof(ResponseType),0);
                if(response == ERROR){
                    continue;
                }

                if (info.isDeleted)
                {
                    printf("File '%s' deleted from the client.\n", info.name);
                    continue;
                }

                if (info.size == 0)
                {   
                    printf("File '%s' sent to the client.\n", info.name);
                    continue;
                }
                
                sendFile(socketFD, &info);

                // Wait for client to receive the file
                if (recv(socketFD, &response, sizeof(ResponseType),0) == -1) {
                    perror("Failed to read file response from client");
                    continue;
                }
            }
        }
        
        free(dir);
        free(diffArray);
    }

    free(entries);
}

void initializeClient(){
    // Create client socket
    socketFD = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFD < 0) {
        perror("Failed to create client socket");
        exit(1);
    }

    struct sockaddr_in serverAddressInfo;
    serverAddressInfo.sin_family = AF_INET;
    serverAddressInfo.sin_port = htons(port);
    if (inet_pton(AF_INET, serverAddress, &(serverAddressInfo.sin_addr)) <= 0) {
        perror("Invalid server address");
        close(socketFD);
        exit(1);
    }
    if (connect(socketFD, (struct sockaddr*)&serverAddressInfo, sizeof(serverAddressInfo)) < 0) {
        perror("Failed to connect to the server");
        close(socketFD);
        exit(1);
    }

    create_directory(directory);

    int cs;
    recv(socketFD,&cs,sizeof(int),0);
    if (cs)
    {   
        printf("Connection accepted!\n");
        entries = (Info*)malloc(max_entries * sizeof(Info));
        synchronizeDirectories();
    }

    else{
        printf("Connection refused!\n");
    }

    free(entries);
    close(socketFD);
}

int main(int argc, char *argv[]) {
    if (argc < 3 || argc > 4) {
        printf("Usage: %s [dirName] [portnumber]\n", argv[0]);
        return 1;
    }

    directory = argv[1];
    if (directory[strlen(directory)-1] == '/')
        directory[strlen(directory)-1] = '\0';

    port = atoi(argv[2]);
    serverAddress = (argc == 4) ? argv[3] : "127.0.0.1";

    initializeClient();

    return 0;
}
