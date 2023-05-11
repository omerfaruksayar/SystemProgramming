#include <semaphore.h>

typedef struct {
    sem_t mutex;
    sem_t full;
    sem_t empty;
    sem_t running;
    int front;
    int rear;
    int capacity;
    pid_t data[];
} Queue;

void initialize(Queue* queue, int capacity, int max) {
    queue->front = -1;
    queue->rear = -1;
    queue->capacity = capacity;
    sem_init(&queue->running, 1, max);
    sem_init(&queue->mutex, 1, 1);
    sem_init(&queue->full, 1, 0);
    sem_init(&queue->empty, 1, capacity);
}

int is_empty(Queue* queue) {
    return (queue->front == -1);
}

int is_full(Queue* queue) {
    return ((queue->rear + 1) % queue->capacity == queue->front);
}

pid_t dequeue(Queue* queue) {

    pid_t item = queue->data[queue->front];
    if (queue->front == queue->rear) {
        queue->front = -1;
        queue->rear = -1;
    } else {
        queue->front = (queue->front + 1) % queue->capacity;
    }
    return item;
}

void enqueue(Queue* queue, pid_t item) {
    if (is_full(queue)) {
        //printf("Error: Queue is full\n");
    } else {
        if (queue->front == -1) {
            queue->front = 0;
        }
        queue->rear = (queue->rear + 1) % queue->capacity;
        queue->data[queue->rear] = item;
    }
}
