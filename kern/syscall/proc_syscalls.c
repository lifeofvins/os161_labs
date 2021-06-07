/*file con implementazione della system call exit
In OS161, you can end user programs by calling the “_exit( )” system call. Without an implementation of _exit( ), the threads created to handle user programs will hang around forever, executing an infinite loop in the user space and taking up a lot of CPU time.
*The function _exit() terminates the calling process "immediately". Any open file descriptors belonging to the process are closed; any children of the process are inherited by process 1, init, and the process's parent is sent a SIGCHLD signal. 
*Tale funzione deve effettuare le chiamate a as_destroy() e thread_exit()
*void as_destroy(struct addrspace *as);
void thread_exit(void);
*
*/
/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys__exit.
 * It just avoids crash/panic. Full process exit still TODO 
 * Address space is released
 */

#include <types.h>
#include <kern/unistd.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>

/*LAB4*/

#include <synch.h> 
#include <current.h>
#define USE_SEM 1 /*LAB4*/

/*
 * simple proc management system calls
 */
void
sys__exit(int status)
{
#if OPT_LAB4

	struct proc *proc = curproc;
	proc->status = status & 0xff; /*just lower 8 bits returned*/

	proc_remthread(curthread); /*rimuovo il thread dal processo prima di segnalare*/
#if USE_SEM
  	V(proc->proc_sem);
#else
	/*condition variable*/
	lock_acquire(proc->proc_lock);
	cv_signal(proc->proc_cv);
	lock_release(proc->proc_lock);
#endif /*USE_SEM*/

#else
   /* get address space of current process and destroy */
  struct addrspace *as = proc_getas();
  as_destroy(as);
  /* thread exits. proc data structure will be lost */
  thread_exit(); /*per il momento si blocca nel ciclo do{} while (next == NULL) all'interno di questa funzione, forse perchè lascio il thread come zombie*/

  panic("thread_exit returned (should not happen)\n");
  (void) status; // TODO: status handling
#endif
}

/*implemento sys_waitpid*/
int sys_waitpid(pid_t pid, userptr_t statusp, int options) {
#if OPT_LAB4
	struct proc *proc;
	proc = proc_search_pid(pid); /*funzione che devo implementare in proc.c*/
	int s;
	(void)options; /*not handled*/
	if (proc == NULL) return -1;
	s = proc_wait(proc);
	if (statusp != NULL) {
		*(int*)statusp = s;
	}
	kprintf("Process %s returned with status %d\n", proc->p_name, s);
	return pid;
#else
	(void)options;
	(void)pid;
	(void)statusp;
	return -1;
#endif	
}



pid_t sys_getpid(void) {
#if OPT_LAB4
	KASSERT(curproc != NULL);
	return curproc->pid;
#else
	return -1;
#endif;
}



