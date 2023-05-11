//This program is a server program that will receive a message from a client and send a message back to the client.
//the Server side would enter the specified directory (create  dirname if the dirname does not exits), 
// create  a  log  file  for  the  clients  and  prompt  its  PID  for  the  clients  to  connect.  The  for  each  client 
// connected will fork a copy of itself in order to serve the specified client (commands are given on the 
// client  side). If  a  kill  signal  is  generated  (either  by  Ctrl-C  or  from  a  client  side  request)  Server  is 
// expected to display the request, send kill signals to its child processes, ensure the log file is created 
// properly and exit.

//Usage: biboServer <dirname> <max. #ofClients> 

#include "../include/asQueue.h"
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

#define CAPACITY 100
#define SHM_SIZE (sizeof(Queue) + (CAPACITY* sizeof(pid_t)))

Queue* queue;

void handle_signal(int signal_num) {
    printf("Connection request PID %d ...  Queue is FULL\n", queue->current);
}

int main(int argc, char const *argv[])
{   
    if(argc < 3)
    {
        printf("Usage: %s <dirname> <max. #ofClients>\n", argv[0]);
        return 1;
    }

    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    int fd = shm_open("OS", O_RDWR | O_CREAT, 0666);
    if (fd == -1) {
        perror("shm_open");
        exit(1);
    }

    if (ftruncate(fd, SHM_SIZE) == -1) {
        perror("ftruncate");
        return 1;
    }

    queue = mmap(NULL, SHM_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (queue == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    if(close(fd) == -1) {
        perror("close");
        return 1;
    }

    sem_t *spot = mmap(NULL, sizeof(*spot), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    int max = atoi(argv[2]);
    sem_init(spot, 1, max);
    initialize(queue, CAPACITY, max);
    int *counter = mmap(NULL, sizeof(*counter), PROT_READ|PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    (*counter) = 1;

    printf("Server is ready to accept connections...\n");
    while(1){
       
        sem_wait(spot);
        pid_t client = dequeue(queue);
        
        if(fork() == 0)
        {   
            int i = (*counter)++;
            printf("Client %d connected as client%d\n", client, i);
            sleep(10);
            printf("Client %d disconnected\n", i);
            queue->spot++;
            sem_post(spot);
            exit(0);
        }
    }

    // Semaförleri sil ve belleği kapat
    sem_destroy(&queue->mutex);
    sem_destroy(&queue->full);
    sem_destroy(&queue->empty);
    sem_destroy(spot);
    
    munmap(spot, sizeof(*spot));
    munmap(queue, SHM_SIZE);
    munmap(counter, sizeof(*counter));
    shm_unlink("OS");
    
    return 0;
}