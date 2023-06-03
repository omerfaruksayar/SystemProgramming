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
#include <signal.h>

#define MAX_PATH_LENGTH 1024

typedef struct {
    int src_fd;
    int dest_fd;
    char src_path[MAX_PATH_LENGTH];
    char dest_path[MAX_PATH_LENGTH];
} Copy;

pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t total_bytes_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t total_files_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buffer_not_full_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t buffer_not_empty_cond = PTHREAD_COND_INITIALIZER;

Copy* buffer;
int buffer_size = 0;
int buffer_capacity = 0;
int buffer_front = 0;
int buffer_rear = 0;
int num_consumers = 0;
long long total_file = 0;
long long total_byte = 0;
long long total_directory = 0;
__sig_atomic_t done = 0;
__sig_atomic_t total_fifo = 0;

pthread_t producer_thread;
pthread_t* consumer_threads;

void init_buffer(int size) {
    buffer = (Copy*)malloc(sizeof(Copy) * size);
    buffer_size = 0;
    buffer_capacity = size;
    buffer_front = 0;
    buffer_rear = 0;
}

int is_buffer_empty() {
    return buffer_size == 0;
}

int is_buffer_full() {
    return buffer_size == buffer_capacity;
}

void enqueue(Copy request) {
    buffer[buffer_rear] = request;
    buffer_rear = (buffer_rear + 1) % buffer_capacity;
    buffer_size++;
}

Copy dequeue() {
    Copy request = buffer[buffer_front];
    buffer_front = (buffer_front + 1) % buffer_capacity;
    buffer_size--;
    return request;
}

void copy_directory(const char* src_dir, const char* dest_dir) {
    DIR* dir;
    struct dirent* entry;
    struct stat statbuf;

    dir = opendir(src_dir);
    if (dir == NULL) {
        perror("opendir");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        char src_file[MAX_PATH_LENGTH];
        char dest_file[MAX_PATH_LENGTH];

        snprintf(src_file, sizeof(src_file), "%s/%s", src_dir, entry->d_name);
        snprintf(dest_file, sizeof(dest_file), "%s/%s", dest_dir, entry->d_name);

        if (stat(src_file, &statbuf) == -1) {
            perror("stat");
            continue;
        }

        if (S_ISDIR(statbuf.st_mode)) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                mkdir(dest_file, statbuf.st_mode);
                pthread_mutex_lock(&total_files_mutex);
                total_directory++;
                pthread_mutex_unlock(&total_files_mutex);
                copy_directory(src_file, dest_file);  // Recursive call to copy subdirectories
            }
        }

        else if (S_ISFIFO(statbuf.st_mode)) {
            if (mkfifo(dest_file, statbuf.st_mode) == -1) {
                perror("mkfifo");
                continue;
            }

            total_fifo = total_fifo + 1;
        }

        else if (S_ISREG(statbuf.st_mode)) {
            int src_fd = open(src_file, O_RDONLY);
            if (src_fd == -1) {
                perror("open");
                continue;
            }

            int dest_fd = open(dest_file, O_WRONLY | O_CREAT | O_TRUNC, statbuf.st_mode);
            if (dest_fd == -1) {
                perror("open");
                close(src_fd);
                continue;
            }

            Copy request;
            request.src_fd = src_fd;
            request.dest_fd = dest_fd;
            strncpy(request.src_path, src_file, sizeof(request.src_path));
            strncpy(request.dest_path, dest_file, sizeof(request.dest_path));

            pthread_mutex_lock(&buffer_mutex);

            while (is_buffer_full()) {
                pthread_cond_wait(&buffer_not_full_cond, &buffer_mutex);
            }

            enqueue(request);

            pthread_cond_signal(&buffer_not_empty_cond);
            pthread_mutex_unlock(&buffer_mutex);
        }
    }

    closedir(dir);
}

void* producer(void* arg) {
    char** directories = (char**)arg;

    if (directories[0][strlen(directories[0]) - 1] == '/')
        directories[0][strlen(directories[0]) - 1] = '\0';

    if (directories[1][strlen(directories[1]) - 1] == '/')
        directories[1][strlen(directories[1]) - 1] = '\0';
    
    copy_directory(directories[0], directories[1]);

    pthread_mutex_lock(&buffer_mutex);
    done = 1;
    pthread_cond_broadcast(&buffer_not_empty_cond);
    pthread_mutex_unlock(&buffer_mutex);

    return NULL;
}

void copy_file(Copy request){

    ssize_t bytes_read = 0;
    ssize_t total_written = 0;
    char cp[4096];

    while ((bytes_read = read(request.src_fd, cp, sizeof(cp))) > 0) {
        ssize_t bytes_written = write(request.dest_fd, cp, bytes_read);
        if (bytes_written != bytes_read) {
            perror("write");
            break;
        }
        total_written += bytes_written;
    }

    close(request.src_fd);
    close(request.dest_fd);

    pthread_mutex_lock(&total_bytes_mutex);
    total_file++;
    total_byte += total_written;
    pthread_mutex_unlock(&total_bytes_mutex);

    pthread_mutex_lock(&output_mutex);
    printf("Copied:%s ", request.src_path);
    printf("Size: %ld bytes\n", total_written);
    fflush(stdout);
    pthread_mutex_unlock(&output_mutex);

}

void* consumer(void* arg) {
    while (1) {

        pthread_mutex_lock(&buffer_mutex);

        while (is_buffer_empty() && !done) {
            pthread_cond_wait(&buffer_not_empty_cond, &buffer_mutex);
        }

        if (is_buffer_empty() && done) {
            pthread_mutex_unlock(&buffer_mutex);
            break;
        }

        Copy request = dequeue();
        pthread_cond_signal(&buffer_not_full_cond);
        pthread_mutex_unlock(&buffer_mutex);

        copy_file(request);
    }
    pthread_exit(NULL);
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

void signal_handler(int signum){

    pthread_mutex_lock(&buffer_mutex);
    done = 1;
    pthread_cond_broadcast(&buffer_not_empty_cond);
    pthread_mutex_unlock(&buffer_mutex);

    pthread_cancel(producer_thread);
    for (int i = 0; i < num_consumers; i++) {
        pthread_cancel(consumer_threads[i]);
    }

    // Wait for producer and consumer threads to terminate
    pthread_join(producer_thread, NULL);
    for (int i = 0; i < num_consumers; i++) {
        pthread_join(consumer_threads[i], NULL);
    }

    free(buffer);
    free(consumer_threads);

    printf("Exit signal received! \n");
    printf("Total number of regular files: %lld\n", total_file);
    printf("Total number of directories: %lld\n", total_directory);
    printf("Total number of bytes: %lld\n", total_byte);
    printf("Total number of FIFOs: %d\n", total_fifo);

    exit(signum);
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        printf("Usage: %s <buffer_size> <num_consumers> <source_dir> <dest_dir>\n", argv[0]);
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);

    buffer_size = atoi(argv[1]);
    num_consumers = atoi(argv[2]);
    char** directories = &argv[3];

    init_buffer(buffer_size);
    
    consumer_threads = (pthread_t*)malloc(num_consumers * sizeof(pthread_t));

    if (create_directory(directories[1]) == -1) {
        printf("Failed to create destination directory: %s\n", directories[1]);
        return 1;
    }

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
    double elapsed_time = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_usec - start_time.tv_usec) / 1000000.0;

    printf("Total time taken: %.2f seconds\n", elapsed_time);
    printf("Total regular file copied: %lld\n", total_file);
    printf("Total directory copied: %lld\n", total_directory);
    printf("Total FIFO copied: %d\n", total_fifo);
    printf("Total byte copied: %lld\n", total_byte);

    free(buffer);
    free(consumer_threads);

    return 0;
}