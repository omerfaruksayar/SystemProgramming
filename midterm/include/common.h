#include <stdio.h>
#include <stdlib.h>

typedef struct node {
    int data;
    struct node* next;
} node_t;

typedef struct {
    node_t* head;
    node_t* tail;
} queue_t;

typedef struct connectionReq {
    pid_t client_pid;
    int try_flag;
} connectionReq;

typedef struct connectionRes {
    int child_pid;
    int num;
    char* working_dir;
} connectionRes;

void init_queue(queue_t* q) {
    q->head = NULL;
    q->tail = NULL;
}

int is_queue_empty(queue_t* q) {
    return (q->head == NULL);
}

void enqueue(queue_t* q, int data) {
    node_t* new_node = (node_t*)malloc(sizeof(node_t));
    new_node->data = data;
    new_node->next = NULL;
    if (is_queue_empty(q)) {
        q->head = new_node;
        q->tail = new_node;
    } else {
        q->tail->next = new_node;
        q->tail = new_node;
    }
}

int dequeue(queue_t* q) {
    if (is_queue_empty(q)) {
        printf("Error: Queue is empty\n");
        return -1;
    }
    int data = q->head->data;
    node_t* temp = q->head;
    q->head = q->head->next;
    if (q->head == NULL) {
        q->tail = NULL;
    }
    free(temp);
    return data;
}
