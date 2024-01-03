/* COMP 310/ECSE427 ASSIGNMENT 2

	AUTHOR: WILLIAM ZHANG (260975150)

*/


#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <ucontext.h>
#include <pthread.h>
#include <unistd.h>
#include "queue.h"
#include "sut.h"

#define BUFSIZE 4096
#define STACK_SIZE 1024*64

// task structure
struct sut_task{
	ucontext_t context;
	int id;
};

// Variable initializations.

// 30 tasks max
struct sut_task tasks[30];

// ready queue
struct queue ready_queue;

// waiting queue
struct queue wait_queue;

// io requests queue
struct queue toio_queue;

// io responses queue
struct queue fromio_queue;

// threads initialization
pthread_t c_exec, i_exec;

// contexts initialization;
ucontext_t c_exec_context;
ucontext_t i_exec_context;

// mutex initialization
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t io_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t c_mutex = PTHREAD_MUTEX_INITIALIZER;

// task id
int current_task_id = -1;
int i = 0;

bool ready_queue_empty = false;
bool wait_queue_empty = false;
bool first_task_done = false;


// Helper function to initialize the context
void initialize_context(ucontext_t *context, void(*fn)(void), void *stack){
	getcontext(context);
	context -> uc_link = NULL;
	context -> uc_stack.ss_sp = stack;
	context -> uc_stack.ss_size = STACK_SIZE;
	makecontext(context, fn , 0);
}


// C-EXEC THREAD
void* c_exec_thread(void* arg){
	while (!ready_queue_empty || !wait_queue_empty){
		// PEEK FRONT TO SEE IF THERE IS A TASK
		pthread_mutex_lock(&c_mutex);
		struct queue_entry* head = queue_peek_front(&ready_queue);
		pthread_mutex_unlock(&c_mutex);
		if (head != NULL){
			// GET DATA AND CHANGE CONTEXT TO THE TASK'S CONTEXT
			struct sut_task *next_task = (struct sut_task*) head -> data;
			pthread_mutex_lock(&c_mutex);
			queue_pop_head(&ready_queue);
			pthread_mutex_unlock(&c_mutex);
			current_task_id = next_task -> id;
			swapcontext(&c_exec_context, &next_task->context);

			first_task_done = true;
		}
		else if (first_task_done){
			ready_queue_empty = true;
		}
		// SLEEP FOR A BIT IF THERE IS NO TASK (NO BUSY WAITING)
		usleep(100000);
	}
	return NULL;
}

void* i_exec_thread(void* arg){
	char tmp_msg[BUFSIZE];
	char msg[BUFSIZE];
	char rec_msg[BUFSIZE];
	while (!ready_queue_empty || !wait_queue_empty){
		// PEEK FRONT TO SEE IF THERE IS AN IO REQUEST
		pthread_mutex_lock(&io_mutex);
		struct queue_entry* head_fn = queue_peek_front(&toio_queue);
		pthread_mutex_unlock(&io_mutex);
		if (head_fn != NULL){
			// GET MESSAGE FROM THE IO REQUEST QUEUE
			sprintf(tmp_msg, "%s", (char*)head_fn ->data);
			strcpy(msg, tmp_msg);
			pthread_mutex_lock(&io_mutex);
			queue_pop_head(&toio_queue);
			pthread_mutex_unlock(&io_mutex);
			char * action = strtok(msg, "*");
			// CHECK WHICH IO FUNCTION TO CALl
			if (strcmp(action, "open") == 0){
				char *dest = strtok(NULL, "");
				// CREATE OR OPEN FILE
				int fd = open(dest, O_RDWR | O_CREAT, 0644); // CALL SUDO
				if (fd == -1){
					perror("open");
				}
				pthread_mutex_lock(&io_mutex);
				struct queue_entry *resp = queue_new_node(&fd);
				queue_insert_tail(&fromio_queue, resp);
				pthread_mutex_unlock(&io_mutex);
				// TRANSFER TASK FROM WAITING QUEUE TO READY QUEUE
				pthread_mutex_lock(&mutex);
				struct queue_entry *ready = queue_peek_front(&wait_queue);
				if (ready){
					queue_insert_tail(&ready_queue, ready);
					queue_pop_head(&wait_queue);
				}
				pthread_mutex_unlock(&mutex);
			}
			if (strcmp(action, "write") == 0){
				printf("\nWRITE\n");
				int fd = atoi(strtok(NULL, "*"));
				char* buf = strtok(NULL, "*");
				int size = atoi(strtok(NULL, ""));
				ssize_t bytes_written = write(fd, buf, size); // CALL SUDO
				if (bytes_written == -1){
					perror("write");
				}
				pthread_mutex_lock(&mutex);
				struct queue_entry *ready = queue_peek_front(&wait_queue);
				if (ready){
					queue_insert_tail(&ready_queue, ready);
					queue_pop_head(&wait_queue);
				}
				pthread_mutex_unlock(&mutex);
			}
			if (strcmp(action, "read")==0){
				printf("\nREAD\n");
				int fd = atoi(strtok(NULL, "*"));
				char* buf = strtok(NULL, "*");
				int size = atoi(strtok(NULL, ""));
				ssize_t bytes_read = read(fd, buf, size); // CALL SUDO
				if (bytes_read == -1){
					perror("read");
				}
				pthread_mutex_lock(&io_mutex);
				struct queue_entry *resp = queue_new_node(&buf);
				queue_insert_tail(&fromio_queue, resp);
				pthread_mutex_unlock(&io_mutex);
				pthread_mutex_lock(&mutex);
				struct queue_entry *ready = queue_peek_front(&wait_queue);
				if (ready){
					queue_insert_tail(&ready_queue, ready);
					queue_pop_head(&wait_queue);
				}
				pthread_mutex_unlock(&mutex);
			}
			if (strcmp(action, "close") == 0){
				printf("\nCLOSE\n");
				int fd = atoi(strtok(NULL, "*"));
				// CLOSE FILE
				close(fd);
				pthread_mutex_lock(&mutex);
				struct queue_entry *ready = queue_peek_front(&wait_queue);
				if (ready){
					queue_insert_tail(&ready_queue, ready);
					queue_pop_head(&wait_queue);
				}
				pthread_mutex_unlock(&mutex);
			}

		}
		else if (first_task_done){
			wait_queue_empty = true;
		}
		// SLEEP IF NO REQUEST IS IN THE QUEUE
		usleep(100000);
	}
	return 0;
}


void sut_init(){
	// INITIALIZE SUT
	pthread_mutex_init(&mutex, NULL);
	queue_init(&ready_queue);
	queue_init(&wait_queue);
	queue_init(&toio_queue);
	queue_init(&fromio_queue);
	if (pthread_create(&c_exec, NULL, c_exec_thread, NULL) != 0){
		perror("pthread_create");
	}
	if (pthread_create(&i_exec, NULL, i_exec_thread, NULL) != 0){
		perror("pthread_create");
	}
}

// CREATE TASKS AND ENQUEUE
bool sut_create(sut_task_f fn){
	// UNIQUE ID FOR EACH TASK
	int new_task_id = i++;
	struct sut_task *new_task = &tasks[new_task_id];

	new_task -> id = new_task_id;
	void* stack = malloc(STACK_SIZE);
	if (stack == NULL){
		return false;
	}
	initialize_context(&new_task -> context, fn, stack);
	pthread_mutex_lock(&mutex);
	struct queue_entry *new_entry = queue_new_node( new_task);
	queue_insert_tail(&ready_queue, new_entry);
	pthread_mutex_unlock(&mutex);
	return true;
}

// YIELD CONTEXT TO C-EXEC THREAD
void sut_yield(){
	ucontext_t current_context;
	getcontext(&current_context);
	pthread_mutex_lock(&c_mutex);
	struct queue_entry *current_entry = queue_new_node(&current_context);
	queue_insert_tail(&ready_queue, current_entry);
	pthread_mutex_unlock(&c_mutex);
	swapcontext(&current_context, &c_exec_context);

}
// EXIT TASK CONTEXT FOR GOOD.
void sut_exit(){
	ucontext_t current_context;
	getcontext(&current_context);
	swapcontext(&current_context, &c_exec_context);
}

// SEND IO REQUEST FOR OPEN AND CHECK FOR FD WHEN CONTEXT IS RESTORED
int sut_open(char *file_name){
	char tmp[BUFSIZE];
	ucontext_t current_context;
	getcontext(&current_context);
	pthread_mutex_lock(&mutex);
	struct queue_entry *current_entry = queue_new_node(&current_context);
	queue_insert_tail(&wait_queue, current_entry);
	pthread_mutex_unlock(&mutex);
	pthread_mutex_lock(&io_mutex);
	sprintf(tmp, "open*%s", file_name);
	char *msg = strdup(tmp);
	struct queue_entry *current_fn = queue_new_node(msg);
	queue_insert_tail(&toio_queue, current_fn);
	pthread_mutex_unlock(&io_mutex);
	swapcontext(&current_context, &c_exec_context);
	
	struct queue_entry* from_io = queue_peek_front(&fromio_queue);
	if(from_io != NULL){
		int* response = (int* )from_io -> data;
		printf("%d", *response);
		pthread_mutex_lock(&c_mutex);
		queue_pop_head(&fromio_queue);
		pthread_mutex_unlock(&c_mutex);
		return *response;
	}
	return -1;

// SEND CLOSE IO REQUEST.
}
void sut_close(int fd){
	char tmp[BUFSIZE];
	ucontext_t current_context;
	getcontext(&current_context);
	pthread_mutex_lock(&mutex);
	struct queue_entry *current_entry = queue_new_node(&current_context);
	queue_insert_tail(&wait_queue, current_entry);
	pthread_mutex_unlock(&mutex);
	pthread_mutex_lock(&io_mutex);
	sprintf(tmp, "close*%d",fd);
	char *msg = strdup(tmp);
	struct queue_entry *current_fn = queue_new_node(msg);
	queue_insert_tail(&toio_queue, current_fn);
	pthread_mutex_unlock(&io_mutex);
	swapcontext(&current_context, &c_exec_context);
}

// SEND WRITE IO REQUEST WITH INFORMATION
void sut_write(int fd, char *buf, int size){
	char tmp[BUFSIZE];
	ucontext_t current_context;
	getcontext(&current_context);
	pthread_mutex_lock(&mutex);
	struct queue_entry *current_entry = queue_new_node(&current_context);
	queue_insert_tail(&wait_queue, current_entry);
	pthread_mutex_unlock(&mutex);
	pthread_mutex_lock(&io_mutex);
	sprintf(tmp, "write*%d*%s*%d",fd, buf, size);
	char *msg = strdup(tmp);
	struct queue_entry *current_fn = queue_new_node(msg);
	queue_insert_tail(&toio_queue, current_fn);
	pthread_mutex_unlock(&io_mutex);
	swapcontext(&current_context, &c_exec_context);
}


// SEND READ IO REQUEST WITH INFORMATION AND PROBE FOR MESSAGE ONCE CONTEXT IS RESTORED
char* sut_read(int fd, char *buf, int size){
	char tmp[BUFSIZE];
	ucontext_t current_context;
	getcontext(&current_context);
	pthread_mutex_lock(&mutex);
	struct queue_entry *current_entry = queue_new_node(&current_context);
	queue_insert_tail(&wait_queue, current_entry);
	pthread_mutex_unlock(&mutex);
	pthread_mutex_lock(&io_mutex);
	sprintf(tmp, "read*%d*%s*%d",fd, buf, size);
	char *msg = strdup(tmp);
	struct queue_entry *current_fn = queue_new_node(msg);
	queue_insert_tail(&toio_queue, current_fn);
	pthread_mutex_unlock(&io_mutex);
	swapcontext(&current_context, &c_exec_context);

	struct queue_entry* from_io = queue_peek_front(&fromio_queue);
	if(from_io != NULL){
		char* response = (char* )from_io -> data;
		printf("%s", response);
		pthread_mutex_lock(&c_mutex);
		queue_pop_head(&fromio_queue);
		pthread_mutex_unlock(&c_mutex);
		return response;
	}
	return NULL;
}

// JOIN EACH PTHREAD TO SHUT DOWN.
void sut_shutdown(){
	pthread_join(c_exec, NULL);
	pthread_join(i_exec, NULL);
}


