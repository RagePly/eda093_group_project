/*
 * Exercise on thread synchronization.
 *
 * Assume a half-duplex communication bus with limited capacity, measured in tasks, and 2 priority levels:
 *
 * - tasks: A task signifies a unit of data communication over the bus
 *
 * - half-duplex: All tasks using the bus should have the same direction
 *
 * - limited capacity: There can be only 3 tasks using the bus at the same time.
 *                     In other words, the bus has only 3 slots.
 *
 *  - 2 priority levels: Priority tasks take precedence over non-priority tasks
 *
 *  Fill-in your code after the TODO comments
 */

#include <stdio.h>
#include <string.h>

#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "timer.h"

/* This is where the API for the condition variables is defined */
#include "threads/synch.h"

/* This is the API for random number generation.
 * Random numbers are used to simulate a task's transfer duration
 */
#include "lib/random.h"

#define MAX_NUM_OF_TASKS 200

#define BUS_CAPACITY 3

typedef enum {
  SEND,
  RECEIVE,

  NUM_OF_DIRECTIONS
} direction_t;

typedef enum {
  NORMAL,
  PRIORITY,

  NUM_OF_PRIORITIES
} priority_t;

typedef struct {
  direction_t direction;
  priority_t priority;
  unsigned long transfer_duration;
} task_t;

void init_bus (void);
void batch_scheduler (unsigned int num_priority_send,
                      unsigned int num_priority_receive,
                      unsigned int num_tasks_send,
                      unsigned int num_tasks_receive);

/* Thread function for running a task: Gets a slot, transfers data and finally
 * releases slot */
static void run_task (void *task_);

/* WARNING: This function may suspend the calling thread, depending on slot
 * availability */
static void get_slot (const task_t *task);

/* Simulates transfering of data */
static void transfer_data (const task_t *task);

/* Releases the slot */
static void release_slot (const task_t *task);

/* The capacity of the bus: number of tasks occupying it*/
static int tasks;

/* Current direction of the bus */
static direction_t direction;

/* The bus-lock */
static struct lock bus;

/* Condition variables; conditions for each category 
 * direction_t x priority_t */
static struct condition waitingToGo[2][2];

/* Total number of priority tasks that are currently waiting */
static int waiting_prio;


void init_bus (void) {

  random_init ((unsigned int)123456789);

  /* Initialize the bus-lock */
  lock_init(&bus);

  /* Initialize condition variables */
  cond_init(&waitingToGo[0][0]);
  cond_init(&waitingToGo[0][1]);
  cond_init(&waitingToGo[1][0]);
  cond_init(&waitingToGo[1][1]);

  /* Initialize all counter/state variables */
  tasks = 0;
  direction = NUM_OF_DIRECTIONS;
  waiting_prio = 0;
}

void batch_scheduler (unsigned int num_priority_send,
                      unsigned int num_priority_receive,
                      unsigned int num_tasks_send,
                      unsigned int num_tasks_receive) {
  ASSERT (num_tasks_send + num_tasks_receive + num_priority_send +
             num_priority_receive <= MAX_NUM_OF_TASKS);

  static task_t tasks[MAX_NUM_OF_TASKS] = {0};

  char thread_name[32] = {0};

  unsigned long total_transfer_dur = 0;

  int j = 0;

  /* create priority sender threads */
  for (unsigned i = 0; i < num_priority_send; i++) {
    tasks[j].direction = SEND;
    tasks[j].priority = PRIORITY;
    tasks[j].transfer_duration = random_ulong() % 244;

    total_transfer_dur += tasks[j].transfer_duration;

    snprintf (thread_name, sizeof thread_name, "sender-prio");
    thread_create (thread_name, PRI_DEFAULT, run_task, (void *)&tasks[j]);

    j++;
  }

  /* create priority receiver threads */
  for (unsigned i = 0; i < num_priority_receive; i++) {
    tasks[j].direction = RECEIVE;
    tasks[j].priority = PRIORITY;
    tasks[j].transfer_duration = random_ulong() % 244;

    total_transfer_dur += tasks[j].transfer_duration;

    snprintf (thread_name, sizeof thread_name, "receiver-prio");
    thread_create (thread_name, PRI_DEFAULT, run_task, (void *)&tasks[j]);

    j++;
  }

  /* create normal sender threads */
  for (unsigned i = 0; i < num_tasks_send; i++) {
    tasks[j].direction = SEND;
    tasks[j].priority = NORMAL;
    tasks[j].transfer_duration = random_ulong () % 244;

    total_transfer_dur += tasks[j].transfer_duration;

    snprintf (thread_name, sizeof thread_name, "sender");
    thread_create (thread_name, PRI_DEFAULT, run_task, (void *)&tasks[j]);

    j++;
  }

  /* create normal receiver threads */
  for (unsigned i = 0; i < num_tasks_receive; i++) {
    tasks[j].direction = RECEIVE;
    tasks[j].priority = NORMAL;
    tasks[j].transfer_duration = random_ulong() % 244;

    total_transfer_dur += tasks[j].transfer_duration;

    snprintf (thread_name, sizeof thread_name, "receiver");
    thread_create (thread_name, PRI_DEFAULT, run_task, (void *)&tasks[j]);

    j++;
  }

  /* Sleep until all tasks are complete */
  timer_sleep (2 * total_transfer_dur);
}

/* Thread function for the communication tasks */
void run_task(void *task_) {
  task_t *task = (task_t *)task_;

  get_slot (task);

  msg ("%s acquired slot", thread_name());
  transfer_data (task);

  release_slot (task);
}

static direction_t other_direction(direction_t this_direction) {
  return this_direction == SEND ? RECEIVE : SEND;
}

void get_slot (const task_t *task) {
  lock_acquire(&bus);

  /* If task has priority, increase the *total* number of waiting priority tasks */
  if (task->priority == PRIORITY)
  {
    waiting_prio ++;
  }

  /* Await a slot, which is only possible if the bus has capacity, it's either empty
   * or in the same direction and there are no higher priority tasks waiting */
  while (tasks == BUS_CAPACITY || 
      (tasks > 0 && other_direction(task->direction) == direction) || 
      (task->priority == NORMAL && waiting_prio > 0))
  {
      cond_wait(&waitingToGo[task->direction][task->priority], &bus);
  }


  /* This task can execute, thus increase the utilization of bus and also set the
   * direction to be eequal to this task's direction */
  tasks ++;
  direction = task->direction;

  /* If task has priority, decrease the *total* number of waiting priority tasks */
  if (task->priority == PRIORITY)
  {
    waiting_prio --;

    /* There are no high priority tasks waiting, the bus has capacity and there are 
     * normal priority tasks waiting; wake up normal tasks to allow them to run */
    if (waiting_prio == 0 &&
        tasks < BUS_CAPACITY &&
        !list_empty(&waitingToGo[direction][NORMAL].waiters))
    {
      /* broadcast, since it could be more than one task waiting and the capacity could
       * allow it */
      cond_broadcast(&waitingToGo[direction][NORMAL], &bus);
    }
  }

  lock_release(&bus);
}

void transfer_data (const task_t *task) {
  /* Simulate bus send/receive */
  timer_sleep (task->transfer_duration);
}

void release_slot (const task_t *task) {
  lock_acquire(&bus);

  /* Decrase the utilization of the bus */
  tasks --;

  /* Wake up high priority tasks in the same direction */
  if (!list_empty(&waitingToGo[task->direction][PRIORITY].waiters))
  {
    cond_signal(&waitingToGo[task->direction][PRIORITY], &bus);
  }

  /* If there are no priority tasks waiting in the other direction, wake up
   * normal tasks in the same direction */
  else if (list_empty(&waitingToGo[other_direction(task->direction)][PRIORITY].waiters) 
      && !list_empty(&waitingToGo[task->direction][NORMAL].waiters))
  {
    cond_signal(&waitingToGo[task->direction][NORMAL], &bus);
  }

  /* This bus is open, let through tasks in the other direction */
  else if (tasks == 0)
  {
    /* Wake up all high priority tasks in the other direction, if there are any ... */
    if (!list_empty(&waitingToGo[other_direction(task->direction)][PRIORITY].waiters))
    {
      /* NOTE: if normal tasks are waiting, they will be woken up once all
       * priority tasks are scheduled by get_slot () */
      cond_broadcast(&waitingToGo[other_direction(task->direction)][PRIORITY], &bus);
    }

    /* ... otherwise wake up all normal priority tasks in the other direction */
    else
    {
      cond_broadcast(&waitingToGo[other_direction(task->direction)][NORMAL], &bus);
    }
  }

  lock_release(&bus);
}
