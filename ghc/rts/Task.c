/* -----------------------------------------------------------------------------
 *
 * (c) The GHC Team 2001-
 *
 * The task manager subsystem.  Tasks execute STG code, with this
 * module providing the API which the Scheduler uses to control their
 * creation and destruction.
 *
 * Two kinds of RTS builds uses 'tasks' - the SMP and the
 * 'native thread-friendly' builds. 
 * 
 * The SMP build lets multiple tasks concurrently execute STG code,
 * all sharing vital internal RTS data structures in a controlled manner
 * (see details elsewhere...ToDo: fill in ref!)
 *
 * The 'threads' build has at any one time only one task executing STG
 * code, other tasks are either busy executing code outside the RTS
 * (e.g., a C call) or waiting for their turn to (again) evaluate some
 * STG code. A task relinquishes its RTS token when it is asked to
 * evaluate an external (C) call.
 *
 * -------------------------------------------------------------------------*/
#include "Rts.h"
#if defined(RTS_SUPPORTS_THREADS) /* to the end */
#include "RtsUtils.h"
#include "OSThreads.h"
#include "Task.h"
#include "Stats.h"
#include "RtsFlags.h"
#include "Schedule.h"

/* There's not all that much code that is shared between the
 * SMP and threads version of the 'task manager.' A sign
 * that the code ought to be structured differently..(Maybe ToDo).
 */

/* 
 * The following Task Manager-local variables are assumed to be
 * accessed with the RTS lock in hand.
 */
#if defined(SMP)
static TaskInfo* taskTable;
#endif
/* upper bound / the number of tasks created. */
static nat maxTasks;  
/* number of tasks currently created */
static nat taskCount; 


#if defined(SMP)
void
startTaskManager( nat maxCount, void (*taskStart)(void) )
{
  nat i;
  static int initialized = 0;
  
  if (!initialized) {
  
    taskCount = 0;
    maxTasks = maxCount;
    /* allocate table holding task metadata */
  
    if (maxCount > 0) {
      taskTable = stgMallocBytes(maxCount * sizeof(TaskInfo),
			       "startTaskManager:tasks");

      /* and eagerly create them all. */
      for (i = 0; i < maxCount; i++) {
	startTask(taskStart);
	taskCount++;
      }
    }
    initialized = 1;
  }
}

void
startTask ( void (*taskStart)(void) )
{
  int r;
  OSThreadId tid;

  r = createOSThread(&tid,taskStart);
  if (r != 0) {
    barf("startTask: Can't create new task");
  }

  taskTable[taskCount].id = tid;
  taskTable[taskCount].mut_time = 0.0;
  taskTable[taskCount].mut_etime = 0.0;
  taskTable[taskCount].gc_time = 0.0;
  taskTable[taskCount].gc_etime = 0.0;
  taskTable[taskCount].elapsedtimestart = stat_getElapsedTime();

  IF_DEBUG(scheduler,fprintf(stderr,"scheduler: Started task: %ld\n",tid););
  return;
}

void
stopTaskManager ()
{
  nat i;

  /* Don't want to use pthread_cancel, since we'd have to install
   * these silly exception handlers (pthread_cleanup_{push,pop}) around
   * all our locks.
   */
#if 0
  /* Cancel all our tasks */
  for (i = 0; i < RtsFlags.ParFlags.nNodes; i++) {
    pthread_cancel(taskTable[i].id);
  }
  
  /* Wait for all the tasks to terminate */
  for (i = 0; i < maxCount; i++) {
    IF_DEBUG(scheduler,fprintf(stderr,"scheduler: waiting for task %ld\n", 
			       taskTable[i].id));
    pthread_join(taskTable[i].id, NULL);
  }
#endif

  /* Send 'em all a SIGHUP.  That should shut 'em up. */
  await_death = maxCount;
  for (i = 0; i < maxCount; i++) {
    pthread_kill(taskTable[i].id,SIGTERM);
  }
  while (await_death > 0) {
    sched_yield();
  }
  
  return;
}

#else
/************ THREADS version *****************/

void
startTaskManager( nat maxCount, void (*taskStart)(void) )
{
  /* In the threaded case, maxCount is used to limit the
     the creation of worker tasks. Tasks are created lazily, i.e.,
     when the current task gives up the token on executing
     STG code.
  */
  maxTasks = maxCount;
  taskCount = 0;
}

void
startTask ( void (*taskStart)(void) )
{
  int r;
  OSThreadId tid;
  
  /* Locks assumed: rts_mutex */
  
  /* If more than one worker thread is known to be blocked waiting
     on thread_ready_cond, signal it rather than creating a new one.
  */
  if ( rts_n_waiting_tasks > 0) {
    IF_DEBUG(scheduler,fprintf(stderr,
			       "scheduler: startTask: %d tasks waiting, not creating new one.\n", 
			       rts_n_waiting_tasks););
    signalCondition(&thread_ready_cond);
    /* Not needed, but gives more 'interesting' thread schedules when testing */
    yieldThread();
    return;
  }

  /* If the task limit has been reached, just return. */
  if (maxTasks > 0 && taskCount == maxTasks) {
    IF_DEBUG(scheduler,fprintf(stderr,"scheduler: startTask: task limit (%d) reached, not creating new one.\n",maxTasks));
    return;
  }
  

  r = createOSThread(&tid,taskStart);
  if (r != 0) {
    barf("startTask: Can't create new task");
  }
  taskCount++;

  IF_DEBUG(scheduler,fprintf(stderr,"scheduler: startTask: new task %ld (total_count: %d; waiting: %d)\n", tid, taskCount, rts_n_waiting_tasks););
  return;
}

void
stopTaskManager ()
{

}
#endif


#endif /* RTS_SUPPORTS_THREADS */
