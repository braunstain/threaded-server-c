#include "segel.h"
#include "request.h"
#include "queue.h"

//TO DELETE
#include <stdio.h>

// 
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

SafeQueue queue, VIP_queue;
pthread_t *thread_pool;
pthread_t VIP_thread;

pthread_mutex_t vip_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t vip_cond = PTHREAD_COND_INITIALIZER;
int vip_working = 0;

pthread_mutex_t both_empty_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t both_empty_cond = PTHREAD_COND_INITIALIZER;



// HW3: Parse the new arguments too
void getargs(int *port, int *num_of_threads, int *queue_size, char *sched_algo, int argc, char *argv[])
{
    if (argc < 5) {
	fprintf(stderr, "Usage: %s <port>\n", argv[0]);
	exit(1);
    }
    *port = atoi(argv[1]);
	*num_of_threads = atoi(argv[2]);
	*queue_size = atoi(argv[3]);
	strcpy(sched_algo, argv[4]);
	if (*port < 1025 || *num_of_threads < 1 || *queue_size < 1 || 
		(strcmp(sched_algo, "block") != 0 && strcmp(sched_algo, "dt") != 0 && strcmp(sched_algo, "dh") != 0 && strcmp(sched_algo, "bf") != 0 && strcmp(sched_algo, "random") != 0)) {
		fprintf(stderr, "Non valid input\n");
		exit(1);
	}
}
void checkBothQueuesEmpty() {
	pthread_mutex_lock(&both_empty_mutex);
	if (queue->size==0 && VIP_queue->size==0) {
		pthread_cond_signal(&both_empty_cond);
	}
	pthread_mutex_unlock(&both_empty_mutex);
}

int* removeRandomHalf(SafeQueue queue, int* removed_count, int vip_size) {
	pthread_mutex_lock(&queue->mutex);
	int half_count = (queue->size + 1 + vip_size) / 2;
	*removed_count = half_count;
	if (half_count == 0) {
		pthread_mutex_unlock(&queue->mutex);
		return NULL;
	}
	Node* nodes = malloc(queue->size * sizeof(Node));
	Node current = queue->head;
	for (int i = 0; i < queue->size; i++) {
		nodes[i] = current;
		current = current->next;
	}
	int* removed_data = malloc(half_count * sizeof(int));
	int* selected_indexes = malloc(half_count * sizeof(int)); 
	for (int i = 0; i < half_count; i++) {
		selected_indexes[i] = -1;
	}

	for (int i = 0; i < half_count; ) {
		int random_index = rand() % queue->size;
		int already_selected = 0;
		for (int j = 0; j < i; j++) {
			if (selected_indexes[j] == random_index) {
				already_selected = 1;
				break;
			}
		}
		if (already_selected == 0) {
			selected_indexes[i] = random_index;
			removed_data[i] = nodes[random_index]->data->connfd;
			i++;
		}
	}

	for (int i = 0; i < half_count; i++) {
		int index = selected_indexes[i];
		if (index == 0) {
			Node temp = queue->head;
			queue->head = temp->next;
			if (!queue->head) {
				queue->tail = NULL;
			}
			free(temp);
			queue->size--;
		}
		else {
			Node prev = NULL;
			Node curr = queue->head;
			for (int j = 0; j < index; j++) {
				prev = curr;
				curr = curr->next;
			}
			prev->next = curr->next;
			if (curr == queue->tail) {
				queue->tail = prev;
			}
			free(curr);
			queue->size--;
		}
	}

	pthread_cond_broadcast(&queue->full_cond);
	pthread_mutex_unlock(&queue->mutex);
	free(selected_indexes);
	free(nodes);

	pthread_mutex_lock(&total_size_mutex);
	total_size -= half_count;
	pthread_mutex_unlock(&total_size_mutex);
	pthread_cond_broadcast(&total_size_full_cond);
	return removed_data;
}


void *thread_worker_func(void *arg) {
	while (1) {
		pthread_mutex_lock(&vip_mutex);
		//printf("worker lock \n");
		while (vip_working) {
			pthread_cond_wait(&vip_cond, &vip_mutex); // Block
		}
		pthread_mutex_unlock(&vip_mutex);
		Data data = dequeue(queue);
		int connfd = data->connfd;
		struct timeval arrival = data->arrival;
		struct timeval dispatch;
		gettimeofday(&dispatch, NULL);
		//printf("worker unlock \n");
		//printf("Dequeued connection: %d\n", connfd);
		dispatch.tv_sec = (dispatch.tv_sec - arrival.tv_sec);
		dispatch.tv_usec = (dispatch.tv_usec - arrival.tv_usec);
		if (dispatch.tv_usec < 0) {
			dispatch.tv_sec--;
			dispatch.tv_usec += 1000000;
		}
		// only for demostration purpuse
		int isSkip = requestHandle(connfd, arrival, dispatch, arg);
		if (isSkip) {
			Data skip_data = skipDequeue(queue);
			if (skip_data != NULL) {
				int skip_connfd = skip_data->connfd;
				struct timeval skip_arrival = skip_data->arrival;
				struct timeval skip_dispatch;
				gettimeofday(&skip_dispatch, NULL);
				//printf("worker unlock \n");
				//printf("Dequeued connection: %d\n", connfd);
				skip_dispatch.tv_sec = (skip_dispatch.tv_sec - skip_arrival.tv_sec);
				skip_dispatch.tv_usec = (skip_dispatch.tv_usec - skip_arrival.tv_usec);
				if (skip_dispatch.tv_usec < 0) {
					skip_dispatch.tv_sec--;
					skip_dispatch.tv_usec += 1000000;
				}
				requestHandle(skip_connfd, skip_arrival, skip_dispatch, arg);
				Close(skip_connfd);
				free(skip_data);
			}
		}
		Close(connfd);
		free(data);
		checkBothQueuesEmpty();

		pthread_mutex_lock(&total_size_mutex);

		total_size--;

		if (total_size <= queue->MAX_SIZE) {
			pthread_cond_signal(&total_size_full_cond);
		}
		pthread_mutex_unlock(&total_size_mutex);
	}
	free(arg);
	return NULL;
}

void* VIP_thread_func(void* arg) {
	while (1) {
		Data data = dequeue(VIP_queue);
		int connfd = data->connfd;
		struct timeval arrival = data->arrival;
		struct timeval dispatch;
		gettimeofday(&dispatch, NULL);
		//printf("Dequeued VIP connection: %d\n", connfd);

		pthread_mutex_lock(&vip_mutex);
		vip_working = 1;
		pthread_mutex_unlock(&vip_mutex);
		//printf("VIP working \n");

		dispatch.tv_sec = (dispatch.tv_sec - arrival.tv_sec);
		dispatch.tv_usec = (dispatch.tv_usec - arrival.tv_usec);
		if (dispatch.tv_usec < 0) {
			dispatch.tv_sec--;
			dispatch.tv_usec += 1000000;
		}
		requestHandle(connfd, arrival, dispatch, arg);
		Close(connfd);
		free(data);

		//printf("VIP locks2 \n");
		pthread_mutex_lock(&vip_mutex);
		vip_working = 0;
		pthread_cond_broadcast(&vip_cond);
		pthread_mutex_unlock(&vip_mutex);

		checkBothQueuesEmpty();

		pthread_mutex_lock(&total_size_mutex);

		total_size--;

		if (total_size <= queue->MAX_SIZE) {
			pthread_cond_signal(&total_size_full_cond);
		}
		pthread_mutex_unlock(&total_size_mutex);
	}
	free(arg);
	return NULL;
}

int main(int argc, char* argv[])
{
	int listenfd, connfd, port, num_of_threads, queue_size, clientlen;
	struct sockaddr_in clientaddr;
	char sched_algo[6];


	getargs(&port, &num_of_threads, &queue_size, sched_algo, argc, argv);

	// 
	// HW3: Create some threads...
	//
	queue = malloc(sizeof(struct SafeQueue));
	VIP_queue = malloc(sizeof(struct SafeQueue));
	initQueue(queue, queue_size);
	initQueue(VIP_queue, queue_size);
	thread_pool = malloc(sizeof(pthread_t) * num_of_threads);

	for (int i = 0; i < num_of_threads; i++) {
		threads_stats t = malloc(sizeof(threads_stats));
		t->id = i;
		t->stat_req = 0;
		t->dynm_req = 0;
		t->total_req = 0;
		pthread_create(&thread_pool[i], NULL, thread_worker_func, t);
	}
	threads_stats vip_t = malloc(sizeof(threads_stats));
	vip_t->id = num_of_threads;
	vip_t->stat_req = 0;
	vip_t->dynm_req = 0;
	vip_t->total_req = 0;
	pthread_create(&VIP_thread, NULL, VIP_thread_func, vip_t);

	listenfd = Open_listenfd(port);
	while (1) {
		clientlen = sizeof(clientaddr);
		connfd = Accept(listenfd, (SA*)&clientaddr, (socklen_t*)&clientlen);
		struct timeval arrival;
		gettimeofday(&arrival, NULL);
		if (getRequestMetaData(connfd)) {
			pthread_mutex_lock(&total_size_mutex);
			total_size++;
			pthread_mutex_unlock(&total_size_mutex);
			enqueue(VIP_queue, connfd, "block", arrival);
		}
		else if (total_size >= queue->MAX_SIZE && strcmp(sched_algo, "dt") == 0) {
			if (getRequestMetaData(connfd)) {
				if (queue->size == 0) {
					pthread_mutex_lock(&total_size_mutex);
					total_size++;
					pthread_mutex_unlock(&total_size_mutex);
					enqueue(VIP_queue, connfd, "block", arrival);
				}
				// else {
				// 	Close(DropTail());
				// }
			}
			else {
				Close(connfd);
			}
		}
		else if (total_size >= queue->MAX_SIZE && strcmp(sched_algo, "bf") == 0) {
			while (queue->size != 0 || VIP_queue->size != 0) {
				pthread_cond_wait(&both_empty_cond, &both_empty_mutex);
			}
			pthread_mutex_unlock(&both_empty_mutex);
			Close(connfd);
		}
		else if (total_size >= queue->MAX_SIZE && strcmp(sched_algo, "random") == 0) {
			int removed_count;
			int* removed_items = removeRandomHalf(queue, &removed_count, VIP_queue->size);
			if (removed_items) {
				for (int i = 0; i < removed_count; i++) {
					Close(removed_items[i]);
				}
				free(removed_items);
			}
			pthread_mutex_lock(&total_size_mutex);
			total_size++;
			pthread_mutex_unlock(&total_size_mutex);
			enqueue(queue, connfd, sched_algo, arrival);

		}
		else {
			pthread_mutex_lock(&total_size_mutex);
			total_size++;
			pthread_mutex_unlock(&total_size_mutex);
			enqueue(queue, connfd, sched_algo, arrival);
		}
    }

	destroyQueue(queue);
	destroyQueue(VIP_queue);
	pthread_mutex_destroy(&vip_mutex);
	pthread_cond_destroy(&vip_cond);
	pthread_mutex_destroy(&total_size_mutex);
	free(thread_pool);
	free(queue);
	free(VIP_queue);
	return 0;
}



    


 
