#include "segel.h"
#include "queue.h"

int total_size = 0;
pthread_mutex_t total_size_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t total_size_full_cond = PTHREAD_COND_INITIALIZER;

void initQueue(SafeQueue queue, int MAX_SIZE) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
    queue->MAX_SIZE = MAX_SIZE;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->empty_cond, NULL);
    pthread_cond_init(&queue->full_cond, NULL);
}

void enqueue(SafeQueue queue, int connfd, char* sched, struct timeval arrival) {
    pthread_mutex_lock(&queue->mutex);
    if (total_size > queue->MAX_SIZE) {
        if (strcmp(sched, "block") == 0) {
            while (total_size > queue->MAX_SIZE) {
                pthread_cond_wait(&total_size_full_cond, &queue->mutex);
            }
        }
        else if (strcmp(sched, "dh") == 0) {

            //add to queue first
            Node newNode = malloc(sizeof(Node));
            newNode->data = malloc(sizeof(struct Data));
            newNode->data->connfd = connfd;
            newNode->data->arrival = arrival;
            newNode->next = NULL;

            if (queue->size == 0) {
                queue->head = newNode;
            }
            else {
                queue->tail->next = newNode;
            }
            queue->tail = newNode;

            //remove head
            Node temp = queue->head;
            Data head_data = temp->data;
            int connfd_head = head_data->connfd;

            queue->head = temp->next;
            if (!queue->head) {
                queue->tail = NULL;
            }
            Close(connfd_head);
            free(temp);
            pthread_mutex_unlock(&queue->mutex);

            pthread_mutex_lock(&total_size_mutex);

            total_size--;

            if (total_size <= queue->MAX_SIZE) {
                pthread_cond_signal(&total_size_full_cond);
            }
            pthread_mutex_unlock(&total_size_mutex);

            return;
        }
    }
    Node newNode = malloc(sizeof(Node));
    newNode->data = malloc(sizeof(struct Data));
    newNode->data->connfd = connfd;
    newNode->data->arrival = arrival;
    newNode->next = NULL;

    if (queue->size == 0) {
        queue->head = newNode;
    }
    else {
        queue->tail->next = newNode;
    }
    queue->tail = newNode;
    queue->size++;
    //total_size++;
    //printf("totalsize+ %d /n", total_size);
    pthread_cond_signal(&queue->empty_cond);
    pthread_mutex_unlock(&queue->mutex);
}

Data dequeue(SafeQueue queue) {
    pthread_mutex_lock(&queue->mutex);

    while (queue->size == 0) {
        pthread_cond_wait(&queue->empty_cond, &queue->mutex);
    }

    Node temp = queue->head;
    Data data = temp->data;

    queue->head = temp->next;
    if (!queue->head) {
        queue->tail = NULL;
    }

    free(temp);
    queue->size--;
    pthread_cond_signal(&queue->full_cond);
    pthread_mutex_unlock(&queue->mutex);

    /**
    pthread_mutex_lock(&total_size_mutex);

    total_size--;

    if (total_size <= queue->MAX_SIZE) {
        pthread_cond_signal(&total_size_full_cond);
    }
    pthread_mutex_unlock(&total_size_mutex);
    */
    return data;
}

int getSize(SafeQueue queue) {
    pthread_mutex_lock(&queue->mutex);
    int size = queue->size;
    pthread_mutex_unlock(&queue->mutex);
    return size;
}
// void DropTail(SafeQueue queue) {
//     return 5;
// }

Data skipDequeue(SafeQueue queue) {
    pthread_mutex_lock(&queue->mutex);
    if (queue->size == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }
    //edge case for 1 node in queue
    if (queue->size == 1) {
        Data data = queue->head->data;
        Node temp = queue->head;
        queue->head = NULL;
        queue->tail = NULL;
        queue->size--;
        total_size--;
        free(temp);
        pthread_mutex_unlock(&queue->mutex);
        return data;
    }

    Node prev = queue->head;
    Node curr = queue->head->next;
    while (curr != queue->tail) {
        prev = curr;
        curr = curr->next;
    }

    prev->next = NULL;
    queue->tail = prev;
    Data data = curr->data;

    free(curr);
    queue->size--;
    total_size--;
    pthread_mutex_unlock(&queue->mutex);
    return data;
}

void destroyQueue(SafeQueue queue) {
    pthread_mutex_lock(&queue->mutex);

    Node current = queue->head;
    while (current) {
        Node temp = current;
        current = current->next;
        free(temp->data);
        free(temp);
    }

    pthread_mutex_unlock(&queue->mutex);

    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->empty_cond);
    pthread_cond_destroy(&queue->full_cond);
}

