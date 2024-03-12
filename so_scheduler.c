#include "scheduler_struct.h"

static tschedule schedule = NULL;

/*
 * creates and initializes scheduler
 * + time quantum for each thread
 * + number of IO devices supported
 * returns: 0 on success or negative on error
 */
int so_init(unsigned int time_quantum, unsigned int io)
{
	if (time_quantum <= 0 || io < 0 || io > SO_MAX_NUM_EVENTS || schedule != NULL)
		return -1;

	schedule = (tschedule) malloc(sizeof(str_schedule));
	error_exit(schedule == NULL, "schedule allocation");

	schedule->io = io;
	schedule->quantum = time_quantum;
	schedule->threads = NULL;
	schedule->current_thread = NULL;

	/* initializes the semaphore used to wait all threads to end */
	int return_val = sem_init(&(schedule->end_semaphore), 0, 0);

	error_exit(return_val != 0, "end_semaphore init");

	for (int i = 0; i < 6; i++)
		schedule->queues[i] = NULL;

	return 0;
}

/*
 * allocates memory for a new thread; initializes its fields
 * + handler function
 * + priority
 * returns: pointer to this new allocated thread
 */
tthread init_thread(so_handler *func, unsigned int priority)
{
	tthread new_thread = (tthread) malloc(sizeof(thread));

	error_exit(new_thread == NULL, "new_thread allocation");

	new_thread->rem_time = schedule->quantum;
	new_thread->start = func;
	new_thread->priority = priority;
	new_thread->status = NEW;
	new_thread->io_device = -1;

	/* initializes the semaphore used to pause and start the thread */
	int return_val = sem_init(&(new_thread->semaphore), 0, 0);

	error_exit(return_val < 0, "new_thread sem_init");

	return new_thread;
}

/*
 * adds a new thread to the list of threads
 */
void add_thread(tthread new_thread)
{
	tnode t = schedule->threads, prev = NULL;

	/* reaches the end of linked list */
	while (t != NULL) {
		prev = t;
		t = t->next;
	}

	/* allocates a new node */
	t = (tnode) malloc(sizeof(str_node));

	error_exit(t == NULL, "new node allocation");

	/* sets next to NULL and the thread */
	t->next = NULL;
	t->thread = new_thread;

	/* checks if prev node is NULL, so that the new node would be first */
	if (prev != NULL)
		prev->next = t;
	else
		schedule->threads = t;
}

/*
 * creates a new thread and runs it according to the scheduler
 * + handler function
 * + priority
 * returns: tid of the new task if successful or INVALID_TID
 */
tid_t so_fork(so_handler *func, unsigned int priority)
{
	if (priority > SO_MAX_PRIO || func == NULL)
		return INVALID_TID;

	/*
	 * initializes a new thread (structure), adds it to the list of threads
	 * and to the scheduling priority queues
	 */
	tthread new_thread = init_thread(func, priority);

	add_thread(new_thread);

	enqueue(new_thread);

	/* creates the real thread that will run */
	int return_val = pthread_create(&(new_thread->thread_id), NULL, start_thread, new_thread);

	error_exit(return_val != 0, "pthread_create fail");

	/*
	 * if the current running thread is not set, checks the scheduler (is the first thread)
	 * otherwise, there is a thread running so consumes its quantum
	 */
	if (schedule->current_thread == NULL)
		check_scheduler();
	else
		so_exec();

	return new_thread->thread_id;
}

/*
 * waits for an IO device
 * + device index
 * returns: -1 if the device does not exist or 0 on success
 */
int so_wait(unsigned int io)
{
	tthread current_thread = schedule->current_thread;

	if (io >= schedule->io)
		return -1;

	/* sets the running thread on WAITING status and sets its device to io */
	current_thread->status = WAITING;
	current_thread->io_device = io;

	/* consumes quantum for the wait instruction */
	so_exec();

	return 0;
}

/*
 * signals an IO device
 * + device index
 * return the number of tasks woke or -1 on error
 */
int so_signal(unsigned int io)
{
	if (io >= schedule->io)
		return -1;

	/* goes through all threads registered and check if they are waiting for io device */
	tnode t = schedule->threads;
	int counter = 0;

	while (t != NULL) {
		if (t->thread->io_device == io && t->thread->status == WAITING) {
			/* if a thread is found adds it back to the queues to be scheduled */
			t->thread->io_device = -1;
			enqueue(t->thread);
			counter++;
		}
		t = t->next;
	}

	/* consumes quantum for the wait instruction */
	so_exec();

	return counter;
}

/*
 * does whatever operation
 */
void so_exec(void)
{
	tthread current_thread = schedule->current_thread;

	/* decreases quantum */
	current_thread->rem_time--;

	/* check the scheduler to see what thread is next */
	check_scheduler();

	/*
	 * stops the thread by making its semaphore to wait
	 * if the scheduler decides that this thread must continue
	 * the sem_wait should be passed
	 */
	int return_val = sem_wait(&(current_thread->semaphore));

	error_exit(return_val != 0, "exec sem_wait");
}

/*
 * destroys a scheduler
 */
void so_end(void)
{
	if (schedule != NULL) {
		/* goes through the list of threads and clean the memory allocated for them */
		int return_val;
		tnode t = schedule->threads;

		if (t != NULL) {
			/* waits for the semaphore to indicate that all threads have terminated */
			return_val = sem_wait(&(schedule->end_semaphore));
			error_exit(return_val != 0, "end_semaphore wait");
		}

		while (t != NULL) {
			/* main thread joins each thread to be sure that they are terminated */
			return_val = pthread_join(t->thread->thread_id, NULL);
			error_exit(return_val != 0, "pthread_join");

			/* destroy every thread's semaphore */
			return_val = sem_destroy(&(t->thread->semaphore));
			error_exit(return_val != 0, "thread sem_destroy");

			tnode p = t->next;

			/* frees the memory allocated for each thread */
			free(t->thread);
			free(t);

			t = p;
		}

		/* destroys the scheduler's semaphore */
		return_val = sem_destroy(&(schedule->end_semaphore));
		error_exit(return_val != 0, "end_semaphore destroy");

		/* frees the memory allocated for the priority queues and for the scheduler */
		free_queues();
		free(schedule);
		schedule = NULL;
	}
}

/*
 * this is the intermediate handler used to start a thread
 */
void *start_thread(void *arg)
{
	tthread thread = (tthread) arg;

	/*
	 * when a thread starts it must wait to be scheduled
	 * so in this handler it waits for its semaphore to be posted
	 * by a scheduler call made in other thread
	 */
	int return_val = sem_wait(&thread->semaphore);

	error_exit(return_val != 0, "sem_wait at starting thread");

	/* runs its real handler */
	thread->start(thread->priority);

	/* when returns from handler changes its status to TERMINATED and call the scheduler */
	thread->status = TERMINATED;

	check_scheduler();

	return NULL;
}

/*
 * adds a thread to the priority queues of the scheduler
 */
void enqueue(tthread thread)
{
	error_exit(schedule == NULL, "try to add in queues in unitialized schedule");

	error_exit(thread == NULL, "try to add in queues NULL thread");

	/*
	 * in the correct queue (indicated by the thread priority)
	 * goes to the end of the queue and add a new node
	 */
	tnode t = schedule->queues[thread->priority], prev = NULL;

	while (t != NULL) {
		prev = t;
		t = t->next;
	}

	t = (tnode) malloc(sizeof(str_node));
	error_exit(t == NULL, "new node allocation");

	t->next = NULL;
	t->thread = thread;

	if (prev != NULL)
		prev->next = t;
	else
		schedule->queues[thread->priority] = t;

	/* sets the enqueued thread to READY */
	thread->status = READY;
}

/*
 * removes the biggest priority thread from the priority queues of the scheduler
 */
void dequeue(void)
{
	error_exit(schedule == NULL, "try to remove from queues in unitialized schedule");

	/* goes through the queues in the descending order of priority */
	int found = FALSE, f_prior = -1;

	for (int i = 5; i >= 0; i--) {
		if (schedule->queues[i] != NULL) {
			/*
			 * when a queue is found not empty
			 * its first element have the biggest priority
			 */
			found = TRUE;
			f_prior = i;
			break;
		}
	}

	/* if no queue has elements does nothing */
	if (found == TRUE) {
		tnode next = schedule->queues[f_prior]->next;

		/* frees this node's memory */
		free(schedule->queues[f_prior]);
		schedule->queues[f_prior] = next;
	}
}

/*
 * goes through every queue and clean its memory
 */
void free_queues(void)
{
	error_exit(schedule == NULL, "free queues of unitialized schedule");

	for (int i = 0; i < 6; i++) {
		tnode t = schedule->queues[i];

		while (t != NULL) {
			tnode p = t->next;

			free(t);

			t = p;
		}
	}
}

/*
 * does the same thing as dequeue but it does not remove the node
 * instead its thread is returned
 */
tthread get_max_priority_thread(void)
{
	error_exit(schedule == NULL, "search in unitialized schedule");

	int found = FALSE, f_prior = -1;

	for (int i = 5; i >= 0; i--) {
		if (schedule->queues[i] != NULL) {
			found = TRUE;
			f_prior = i;
			break;
		}
	}

	if (found == TRUE)
		return schedule->queues[f_prior]->thread;

	return NULL;
}

/*
 * sets a thread status to RUNNING and releases its semaphore
 * so that it can pass its semaphore waiting
 */
void wake_thread(tthread thread)
{
	thread->status = RUNNING;

	int return_val = sem_post(&(thread->semaphore));

	error_exit(return_val != 0, "sem_post in wake thread");
}

/*
 * dequeues and wakes up next thread
 */
void run_next(tthread next)
{
	dequeue();
	schedule->current_thread = next;
	wake_thread(next);
}

/*
 * dequeues next thread, wakes up it
 * enqueues current thread and resets its quantum
 */
void change_thread(tthread next, tthread current)
{
	dequeue();
	current->rem_time = schedule->quantum;
	enqueue(current);
	schedule->current_thread = next;
	wake_thread(next);
}

/*
 * when a thread must continue running
 * checks if its quantum expired
 * and resets it
 */
void reset_quantum(tthread thread)
{
	if (thread->rem_time <= 0)
		thread->rem_time = schedule->quantum;
}

/*
 * continues a thread's running
 */
void continue_current(void)
{
	reset_quantum(schedule->current_thread);
	wake_thread(schedule->current_thread);
}

/*
 * checks which thread must continue running after some instrcution has been made
 */
void check_scheduler(void)
{
	/* gets current and next threads */
	tthread current_thread = schedule->current_thread;
	tthread next_thread = get_max_priority_thread();

	if (current_thread == NULL) {
		/* if current thread is not set, runs next */
		run_next(next_thread);
	} else {
		if (current_thread->status == TERMINATED || current_thread->status == WAITING) {
			if (next_thread != NULL) {
				/* if there is a next thread, runs it */
				run_next(next_thread);
			} else {
				/*
				 * else, release end semaphore
				 * because every thread is terminated
				 */
				int return_val = sem_post(&(schedule->end_semaphore));

				error_exit(return_val != 0, "end_semaphore post");
			}
		} else {
			/*
			 * if scheduler checking is not performed after so_wait
			 * or after current thread is done
			 */
			if (next_thread == NULL) {
				/* if there is no other thread, continue with the current */
				continue_current();
			} else {
				/* if there is, compare their priorities */
				if (next_thread->priority > current_thread->priority) {
					change_thread(next_thread, current_thread);
				} else {
					/*
					 * if they have same priority and
					 * current thread's quantum expired
					 * run next
					 */
					if (next_thread->priority == current_thread->priority
					 && current_thread->rem_time <= 0)
						change_thread(next_thread, current_thread);
					else
						continue_current();
				}
			}
		}
	}
}
