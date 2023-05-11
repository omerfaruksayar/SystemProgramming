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
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#define CAPACITY 1000
#define SHM_SIZE sizeof(Queue)*CAPACITY

Queue* queue;
int handled = 0;

void handle_signal(int sig) {
    sem_wait(&queue->mutex);
    int running; 
    sem_getvalue(&queue->running, &running);
    if(running == 0)
        printf("Connection request PID %d... Queue is full\n", &queue->data[queue->rear]);
        
    sem_post(&queue->mutex);
}

int main(int argc, char const *argv[])
{   

    if(argc < 3)
    {
        printf("Usage: %s <dirname> <max. #ofClients>\n", argv[0]);
        return 1;
    }

    int max = atoi(argv[2]);
    
    if (signal(SIGUSR1, handle_signal) == SIG_ERR) {
        perror("signal");
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

    queue = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (queue == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    if(close(fd) == -1) {
        perror("close");
        return 1;
    }

    initialize(queue, CAPACITY, max);
    printf("Server is ready to accept connections...\n");
    while(1){
        
        sem_wait(&queue->full);
        sem_wait(&queue->running);
        sem_wait(&queue->mutex);

        if(fork() == 0)
        {   
            pid_t client = dequeue(queue);
            printf("Client %d connected as \n", client);
            sleep(15);
            sem_post(&queue->running);
        }

        else{
            sem_post(&queue->mutex);
            sem_post(&queue->empty);
        }
    }

    // Semaförleri sil ve belleği kapat
    sem_destroy(&queue->mutex);
    sem_destroy(&queue->full);
    sem_destroy(&queue->empty);
    munmap(queue, SHM_SIZE);
    shm_unlink("OS");
    
    return 0;
}
