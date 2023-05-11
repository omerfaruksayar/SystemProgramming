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

#define CAPACITY 100
#define SHM_SIZE (sizeof(Queue) + (CAPACITY* sizeof(pid_t)))

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

    enqueue(queue, item, 0, serverPID);

    printf("Enqueued: %d\n", item);

    return 0;
}
