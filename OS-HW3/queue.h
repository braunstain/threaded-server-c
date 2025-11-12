#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>

typedef struct SafeQueue {
    struct Node* head;
    struct Node* tail;
    pthread_mutex_t mutex;
    pthread_cond_t empty_cond;
    pthread_cond_t full_cond;
    int size;
    int MAX_SIZE;
} *SafeQueue;

extern int total_size;
extern pthread_mutex_t total_size_mutex;
extern pthread_cond_t total_size_full_cond;

typedef struct Node {
    struct Data* data;
    struct Node* next;
} *Node;

typedef struct Data {
    int connfd;
    struct timeval arrival;
} *Data;

void initQueue(SafeQueue queue, int MAX_SIZE);
void enqueue(SafeQueue queue, int connfd, char* sched, struct timeval arrival);
Data dequeue(SafeQueue queue);
int getSize(SafeQueue queue);
Data skipDequeue(SafeQueue queue);
void destroyQueue(SafeQueue queue);
void DropTail(SafeQueue queue);
#endif //QUEUE_H