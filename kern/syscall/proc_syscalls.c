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
#if USE_SEM
	struct proc *proc = curproc;
	struct thread *th = curthread;
	proc->status = status; /*salvo lo stato di uscita del processo*/
	proc_remthread(th); /*rimuovo il thread dal processo prima di segnalare*/
  	V(proc->proc_sem);
  	/*sys__exit terminates the thread, does not destroy the data structure of the process, but simply signals its termination*/
#else
	/*condition variable*/
	lock_acquire(curproc->proc_lock);
	kprintf("SYS EXIT: Proc %s acquired lock.\n", curproc->p_name);
	curproc->status = status; /*salvo lo stato di uscita del processo*/
	cv_signal(curproc->proc_cv, curproc->proc_lock);
	lock_release(curproc->proc_lock);
	kprintf("SYS EXIT: Proc %s released lock.\n", curproc->p_name);	
#endif
  /*call proc_remthread() before signalling the end of the process and modify thread_exit() function so that it accepts a thread already detached from the process*/
  /*signals the end of the process before calling thread_exit()*/
  thread_exit();
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
