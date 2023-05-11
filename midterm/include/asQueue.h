#include <semaphore.h>
#include <signal.h>
#include <stdio.h>

typedef struct {
    sem_t mutex;
    sem_t full;
    sem_t empty;
    int spot;
    int capacity;
    int front;
    int rear;
    pid_t current;
    pid_t data[];
} Queue;

void initialize(Queue* queue, int capacity, int spot) {
    queue->front = -1;
    queue->rear = -1;
    queue->capacity = capacity;
    queue->spot = spot;
    sem_init(&queue->mutex, 1, 1);
    sem_init(&queue->full, 1, 0);
    sem_init(&queue->empty, 1, capacity);
}

pid_t dequeue(Queue* queue) {

    sem_wait(&queue->full);
    sem_wait(&queue->mutex);
    queue->spot--;
    pid_t item = queue->data[queue->front];
    if (queue->front == queue->rear) {
        queue->front = -1;
        queue->rear = -1;
    } else {
        queue->front = (queue->front + 1) % queue->capacity;
    }

    sem_post(&queue->mutex);
    sem_post(&queue->empty);
    return item;
}

void enqueue(Queue* queue, pid_t item, int try, pid_t server) {
    sem_wait(&queue->empty);
    sem_wait(&queue->mutex);
    queue->current = item;
    
    if (queue->spot == 0)
    {   
        kill(server, SIGUSR1);
        if (try)
            return;           
    }
    
    if (queue->front == -1) {
        queue->front = 0;
    }
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->data[queue->rear] = item;
    sem_post(&queue->mutex);
    sem_post(&queue->full);
}
