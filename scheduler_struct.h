#ifndef SCHEDULER_STRUCT_H_
#define SCHEDULER_STRUCT_H_

#include "so_scheduler.h"
#include <stdlib.h>
#include <stdio.h>
#include <semaphore.h>

#define NEW 0
#define READY 1
#define RUNNING 2
#define WAITING 3
#define TERMINATED 4

#define TRUE 1
#define FALSE 0

#define error_exit(condition, msg)				\
	do {								\
		if (condition) {					\
			fprintf(stderr, "(%s, %d): ",			\
					__FILE__, __LINE__);		\
			perror(msg);			\
			exit(EXIT_FAILURE);				\
		}							\
	} while (0)

typedef struct {
	tid_t thread_id;
	unsigned int priority;
	unsigned int rem_time;
	so_handler *start;
	unsigned char status;
	sem_t semaphore;
	unsigned int io_device;
} thread, *tthread;

typedef struct node {
	tthread thread;
	struct node *next;
} str_node, *tnode;

typedef struct {
	tnode threads;
	tnode queues[6];
	unsigned int quantum;
	unsigned int io;
	tthread current_thread;
	sem_t end_semaphore;
} str_schedule, *tschedule;

void *start_thread(void *arg);

tthread init_thread(so_handler *func, unsigned int priority);

void add_thread(tthread new_thread);

void enqueue(tthread thread);

void dequeue(void);

void free_queues(void);

tthread get_max_priority_thread(void);

void wake_thread(tthread thread);

void run_next(tthread next);

void change_thread(tthread next, tthread current);

void continue_current(void);

void check_scheduler(void);

void reset_quantum(tthread thread);

#endif /* SCHEDULER_STRUCT_H_ */

