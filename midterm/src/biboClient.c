#include "../include/asQueue.h"
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#define CAPACITY 1000    
#define SHM_SIZE sizeof(Queue)*CAPACITY

int main(int argc, char const *argv[])
{   
    int serverPID = atoi(argv[1]);
    int fd = shm_open("OS", O_RDWR, 0666);
    Queue* queue = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    
    if (close(fd) == -1)
    {
        perror("close");
        return 1;
    }
    
    pid_t item = getpid();

    sem_wait(&queue->empty);
    sem_wait(&queue->mutex);
    enqueue(queue, item);
    if (kill(serverPID, SIGUSR1) == -1) {
        perror("kill");
        return 1;
    }
    sem_post(&queue->mutex);
    sem_post(&queue->full);

    printf("Enqueued: %d\n", item);

    return 0;
}
