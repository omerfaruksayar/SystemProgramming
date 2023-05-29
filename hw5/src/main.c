#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/time.h>

#define MAX_PATH_LENGTH 256

typedef struct {
    int src_fd;
    int dest_fd;
    char src_path[MAX_PATH_LENGTH];
    char dest_path[MAX_PATH_LENGTH];
} FileCopyRequest;

pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buffer_not_full_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t buffer_not_empty_cond = PTHREAD_COND_INITIALIZER;

FileCopyRequest* buffer;
__sig_atomic_t buffer_count = 0;
__sig_atomic_t buffer_index = 0;
__sig_atomic_t done = 0;
int buffer_size = 0;

void copy_directory(const char* src_dir, const char* dest_dir) {
    DIR* dir;
    struct dirent* entry;
    struct stat statbuf;

    dir = opendir(src_dir);
    if (dir == NULL) {
        perror("Error opening source directory");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        char src_file[MAX_PATH_LENGTH];
        char dest_file[MAX_PATH_LENGTH];

        snprintf(src_file, sizeof(src_file)+2, "%s/%s", src_dir, entry->d_name);
        snprintf(dest_file, sizeof(dest_file)+2, "%s/%s", dest_dir, entry->d_name);

        if (stat(src_file, &statbuf) == -1) {
            perror("Error getting file status");
            continue;
        }

        if (S_ISDIR(statbuf.st_mode)) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                mkdir(dest_file, statbuf.st_mode);
                copy_directory(src_file, dest_file);  // Recursive call to copy subdirectory
            }
        } 
        
        else {
            int src_fd = open(src_file, O_RDONLY);
            if (src_fd == -1) {
                perror("Error opening source file");
                continue;
            }

            int dest_fd = open(dest_file, O_WRONLY | O_CREAT | O_TRUNC, statbuf.st_mode);
            if (dest_fd == -1) {
                perror("Error opening destination file");
                close(src_fd);
                continue;
            }

            pthread_mutex_lock(&buffer_mutex);

            while (buffer_count == buffer_size) {
                pthread_cond_wait(&buffer_not_full_cond, &buffer_mutex);
            }

            FileCopyRequest request;
            request.src_fd = src_fd;
            request.dest_fd = dest_fd;
            strncpy(request.src_path, src_file, sizeof(request.src_path));
            strncpy(request.dest_path, dest_file, sizeof(request.dest_path));

            buffer[buffer_index] = request;
            buffer_index = (buffer_index + 1) % buffer_size;
            buffer_count++;

            pthread_cond_signal(&buffer_not_empty_cond);
            pthread_mutex_unlock(&buffer_mutex);
        }
    }

    closedir(dir);
}

void* producer(void* arg) {
    char** directories = (char**)arg;
    copy_directory(directories[0], directories[1]);

    pthread_mutex_lock(&buffer_mutex);
    done = 1;
    pthread_cond_broadcast(&buffer_not_empty_cond);
    pthread_mutex_unlock(&buffer_mutex);

    return NULL;
}

void* consumer(void* arg) {
    while (1) {
        pthread_mutex_lock(&buffer_mutex);

        while (buffer_count == 0 && !done) {
            pthread_cond_wait(&buffer_not_empty_cond, &buffer_mutex);
        }

        if (buffer_count == 0 && done) {
            pthread_mutex_unlock(&buffer_mutex);
            break;
        }

        FileCopyRequest request = buffer[buffer_index];
        buffer_index = (buffer_index + 1) % buffer_size;
        buffer_count--;

        pthread_cond_signal(&buffer_not_full_cond);
        pthread_mutex_unlock(&buffer_mutex);

        // Copy file
        ssize_t bytes_read;
        char buffer[4096];
        while ((bytes_read = read(request.src_fd, buffer, sizeof(buffer))) > 0) {
            ssize_t bytes_written = write(request.dest_fd, buffer, bytes_read);
            if (bytes_written != bytes_read) {
                perror("Error writing to destination file");
                break;
            }
        }

        close(request.src_fd);
        close(request.dest_fd);

        pthread_mutex_lock(&output_mutex);
        printf("Copied: %s\n", request.src_path);
        pthread_mutex_unlock(&output_mutex);
    }

    pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        printf("Usage: %s <buffer_size> <num_consumers> <source_dir> <dest_dir>\n", argv[0]);
        return 1;
    }

    buffer_size = atoi(argv[1]);
    int num_consumers = atoi(argv[2]);
    char** directories = &argv[3];
    buffer = (FileCopyRequest*)malloc(sizeof(FileCopyRequest)* buffer_size);
    buffer_count = 0;
    buffer_index = 0;

    pthread_t producer_thread;
    pthread_t consumer_threads[num_consumers];

    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    pthread_create(&producer_thread, NULL, producer, directories);

    for (int i = 0; i < num_consumers; i++) {
        pthread_create(&consumer_threads[i], NULL, consumer, NULL);
    }

    pthread_join(producer_thread, NULL);

    for (int i = 0; i < num_consumers; i++) {
        pthread_join(consumer_threads[i], NULL);
    }

    gettimeofday(&end_time, NULL);
    double elapsed_time = (end_time.tv_sec - start_time.tv_sec) +
                          (end_time.tv_usec - start_time.tv_usec) / 1000000.0;

    printf("Total time taken: %.2f seconds\n", elapsed_time);

    free(buffer);

    return 0;
}
