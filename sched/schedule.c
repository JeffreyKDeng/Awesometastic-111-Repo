/* This file contains the scheduling policy for SCHED
 *
 * The entry points are:
 *   do_noquantum:        Called on behalf of process' that run out of quantum
 *   do_start_scheduling  Request to start scheduling a proc
 *   do_stop_scheduling   Request to stop scheduling a proc
 *   do_nice		  Request to change the nice level on a proc
 *   init_scheduling      Called from main.c to set up/prepare scheduling
 */
#include "sched.h"
#include "schedproc.h"
#include <assert.h>
#include <minix/com.h>
#include <machine/archtypes.h>
#include "kernel/proc.h" /* for queue constants */

PRIVATE timer_t sched_timer;
PRIVATE unsigned balance_timeout;

#define BALANCE_TIMEOUT	5 /* how often to balance queues in seconds */

FORWARD _PROTOTYPE( int schedule_process, (struct schedproc * rmp)	);
FORWARD _PROTOTYPE( void balance_queues, (struct timer *tp)		);

#define DEFAULT_USER_TIME_SLICE 200

/*===========================================================================*
 *				do_noquantum				     *
 *===========================================================================*/

PUBLIC int do_noquantum(message *m_ptr)
{
	register struct schedproc *rmp;
	int rv, proc_nr_n;

	if (sched_isokendpt(m_ptr->m_source, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg %u.\n",
		m_ptr->m_source);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
	if (rmp->priority < MIN_USER_Q) {
		rmp->priority += 1; /* lower priority */
	}

	if ((rv = schedule_process(rmp)) != OK) {
		return rv;
	}
	return OK;
}

/*===========================================================================*
 *				do_stop_scheduling			     *
 *===========================================================================*/
PUBLIC int do_stop_scheduling(message *m_ptr)
{
	register struct schedproc *rmp;
	int rv, proc_nr_n;

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	if (sched_isokendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg "
		"%ld\n", m_ptr->SCHEDULING_ENDPOINT);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
	rmp->flags = 0; /*&= ~IN_USE;*/

	return OK;
}

/*===========================================================================*
 *				do_start_scheduling			     *
 *===========================================================================*/
PUBLIC int do_start_scheduling(message *m_ptr)
{
	register struct schedproc *rmp;
	int rv, proc_nr_n, parent_nr_n, nice, i;
	
	/* we can handle two kinds of messages here */
	assert(m_ptr->m_type == SCHEDULING_START || 
		m_ptr->m_type == SCHEDULING_INHERIT);

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	/* Resolve endpoint to proc slot. */
	if ((rv = sched_isemtyendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n))
			!= OK) {
		return rv;
	}
	rmp = &schedproc[proc_nr_n];
	printf("proc_nr_n is: %d \n",proc_nr_n);

	/* Populate process slot */
	rmp->endpoint     = m_ptr->SCHEDULING_ENDPOINT;
	rmp->parent       = m_ptr->SCHEDULING_PARENT;
	rmp->max_priority = 12;
	rmp->tickets = 20;
	rmp->proc_n = proc_nr_n;
	if (rmp->max_priority >= NR_SCHED_QUEUES) {
		return EINVAL;
	}
	
	switch (m_ptr->m_type) {

	case SCHEDULING_START:

		/* We have a special case here for system processes, for which
		 * quanum and priority are set explicitly rather than inherited 
		 * from the parent */
		rmp->priority   = 13;
		rmp->time_slice = (unsigned) m_ptr->SCHEDULING_QUANTUM;
		break;
		
	case SCHEDULING_INHERIT:

		/* Inherit current priority and time slice from parent. Since there
		 * is currently only one scheduler scheduling the whole system, this
		 * value is local and we assert that the parent endpoint is valid */
		if ((rv = sched_isokendpt(m_ptr->SCHEDULING_PARENT,
				&parent_nr_n)) != OK)
			return rv;

		rmp->priority = 13;
		rmp->time_slice = schedproc[parent_nr_n].time_slice;
		break;
		
	default: 
		/* not reachable */
		assert(0);
	}

	/* Take over scheduling the process. The kernel reply message populates
	 * the processes current priority and its time slice */
	if ((rv = sys_schedctl(0, rmp->endpoint, 0, 0)) != OK) {
		printf("Sched: Error taking over scheduling for %d, kernel said %d\n",
			rmp->endpoint, rv);
		return rv;
	}
	rmp->flags = IN_USE;
    schedule_lottery();
    /*for(i =0; &schedproc[i]!=NULL; i++){
              printf("schedproc[i] returns: %d\n", &schedproc[i].tickets);
             }*/
             /* */
	/* Schedule the process, giving it some quantum */
	if ((rv = schedule_process(rmp)) != OK) {
		printf("Sched: Error while scheduling process, kernel replied %d\n",
			rv);
		return rv;
	}

	/* Mark ourselves as the new scheduler.
	 * By default, processes are scheduled by the parents scheduler. In case
	 * this scheduler would want to delegate scheduling to another
	 * scheduler, it could do so and then write the endpoint of that
	 * scheduler into SCHEDULING_SCHEDULER
	 */

	m_ptr->SCHEDULING_SCHEDULER = SCHED_PROC_NR;

	return OK;
}



/*===========================================================================*
 *				do_nice					     *
 *===========================================================================*/
PUBLIC int do_nice(message *m_ptr)
{
	struct schedproc *rmp;
	int rv;
	int proc_nr_n;
	unsigned new_q, old_q, old_max_q;
    
    printf("inside do nice \n");
	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	if (sched_isokendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg "
		"%ld\n", m_ptr->SCHEDULING_ENDPOINT);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
	new_q = (unsigned) m_ptr->SCHEDULING_MAXPRIO;
	if (new_q >= NR_SCHED_QUEUES) {
		return EINVAL;
	}

	/* Store old values, in case we need to roll back the changes */
	old_q     = rmp->priority;
	old_max_q = rmp->max_priority;

	/* Update the proc entry and reschedule the process */
	rmp->max_priority = rmp->priority = new_q;

	if ((rv = schedule_process(rmp)) != OK) {
		/* Something went wrong when rescheduling the process, roll
		 * back the changes to proc struct */
		rmp->priority     = old_q;
		rmp->max_priority = old_max_q;
	}

	return rv;
}

/*===========================================================================*
 *				schedule_process			     *
 *===========================================================================*/
PRIVATE int schedule_process(struct schedproc * rmp)
{
	int rv;
  /* printf("scheduling process ....... \n");
   if(rmp->priority ==13) {
		  printf("It is at 13 right now \n");
		  if(rmp->tickets == 20){
                          printf("has 20 tickets\n");
                          rmp->tickets --;
                          }
		}
	else{
         printf("It is not at 13 !!!! \n");
         }
    if(rmp->tickets ==19){
                    printf("minused 1 ticket from proc: %d\n", rmp->proc_n);
                    }*/
	if ((rv = sys_schedule(rmp->endpoint, rmp->priority,
			rmp->time_slice)) != OK) {
		printf("SCHED: An error occurred when trying to schedule %d: %d\n",
		rmp->endpoint, rv);
	}/**/
	
 /*if(rmp->priority ==13) {
		  printf("Still at 13 right now \n");
		  if(rmp->tickets == 20){
                          printf("has 20 tickets\n");
                          rmp->tickets --;
                          }
		}
	else{
         printf("It is not at 13 !!!! \n");
         }
    if(rmp->tickets ==19){
                    printf("proc: %d   has 19 tickets\n", rmp->proc_n);
                    }*/
                    
                    
                    
	
	

	return rv;
}

/*===========================================================================*
 *				schedule_lottery			     *
 *===========================================================================*/

PRIVATE void schedule_lottery() {

		register struct schedproc *tmp;
		unsigned tickets_total;
	    int i, proc_number;
	    int upper_bound = -1;
        int lower_bound = 0;

       


for ( proc_number = 0, tmp = schedproc; proc_number < NR_PROCS;proc_number++, tmp++){
             if(tmp->priority == 13 && tmp->flags == IN_USE ){
                     printf("proc[%d] is IN_USE   tickets = %d \n", tmp->proc_n, tmp->tickets);
                     tickets_total = tickets_total + tmp->tickets;
                     }
    }
    printf(" total tickets = %d \n" , tickets_total);
    
       /* 
        if (num_proc > 0) {
            
             for(i =0; &schedproc[i]!=NULL; i++){
              tickets_total += schedproc[i].tickets;
             }
              srand(time(NULL));
            draw = (rand() % tickets_total) + 1;
          
             for (i = 0; &schedproc[i]!=NULL; i++) {
                   upper_bound += (schedproc[i].tickets + 1);
                   if (draw <= upper_bound && draw >= lower_bound) {
                            return 15;
                         }
                    lower_bound += (schedproc[i].tickets + 1);
             }
      } */
}

/*===========================================================================*
 *				start_scheduling			     *
 *===========================================================================*/

PUBLIC void init_scheduling(void)
{
    printf("inside init_scheduling \n");
	balance_timeout = BALANCE_TIMEOUT * sys_hz();
	init_timer(&sched_timer);
	set_timer(&sched_timer, balance_timeout, balance_queues, 0);
}

/*===========================================================================*
 *				balance_queues				     *
 *===========================================================================*/

/* This function in called every 100 ticks to rebalance the queues. The current
 * scheduler bumps processes down one priority when ever they run out of
 * quantum. This function will find all proccesses that have been bumped down,
 * and pulls them back up. This default policy will soon be changed.
 */
PRIVATE void balance_queues(struct timer *tp)
{
	struct schedproc *rmp;
	int proc_nr;
	int rv;

	for (proc_nr=0, rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		if (rmp->flags & IN_USE) {
			if (rmp->priority > rmp->max_priority) {
				rmp->priority -= 1; /* increase priority */
				schedule_process(rmp);
			}
		}
	}

	set_timer(&sched_timer, balance_timeout, balance_queues, 0);
}
