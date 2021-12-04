/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys__exit.
 * It just avoids crash/panic. Full process exit still TODO
 * Address space is released
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <mips/trapframe.h>
#include <current.h>
#include <synch.h>
#include <spl.h>
#include <vnode.h>
#include <vfs.h>
#include <test.h>
#include <kern/wait.h>

#define PRINT 0
#define PROJECT 1
#define PROVA 0

/*
 * system calls for process management
 */

/***************************************SYS_EXIT****************************************************/
void sys__exit(int status)
{
/*TODO: check if parent exists or if parent has exited, then we even don't bother fill the exit code, since no one cares*/
#if OPT_WAITPID
	struct proc *p = curproc;
	struct proc *childp = NULL;
	KASSERT(childp == NULL);

	//TODO: close all open files

	p->p_status = status & 0xff; /* just lower 8 bits returned */

	// proc_remthread(curthread);
#if USE_SEMAPHORE_FOR_WAITPID
	p->p_exited = true;	
	V(p->p_sem);
#else
	lock_acquire(p->p_cv_lock);
	p->p_exited = true;			 //setto a true il flag che mi dice se il processo è uscito
	cv_signal(p->p_cv, p->p_cv_lock);
	lock_release(p->p_cv_lock);
#endif /*SEMAPHORE*/
#else
	/* get address space of current process and destroy */
	struct addrspace *as = proc_getas();
	as_destroy(as);
#endif /*OPT_WAITPID*/
	thread_exit();

	panic("thread_exit returned (should not happen)\n");
	(void)status; // TODO: status handling --> exit code
}

/***************************************SYS_WAITPID*************************************************/
int sys_waitpid(pid_t pid, userptr_t statusp, int options, pid_t *retval)
{

#if OPT_WAITPID

	KASSERT(curthread != NULL);
	KASSERT(curproc != NULL);
#if PROJECT
	//options check: we only support WNOHANG
	if (options != 0 && options != WNOHANG)
	{
		*retval = -1;
		return EINVAL;
	}
#endif /*PROJECT*/
	//get the process associated with the given pid
	struct proc *p = proc_search_pid(pid, retval);
	int s;

#if PROJECT
	if (*retval < 0) {
		//the pid doesn't exist
		return ESRCH; //the pid argument named a nonexistent process
	}

	//sys_waitpid returns error if the calling process doesn't have any child
	if (curproc->p_children->num == 0) {
		*retval = -1;
		return ECHILD;
	}
	//if the pid exists, are we allowed to wait for it? i.e, is it our child?
	if (curproc != p->p_parent) {
		*retval = -1;
		return ECHILD;
	}

	//if WNOHANG was given, and said process is not yet dead, we immediately return 0
	if (options == WNOHANG && !p->p_exited) {
		*retval = 0;
		return 0;
	}

	//invalid pid check
	if (pid > PID_MAX || pid < PID_MIN) {
		*retval = -1;
		return EINVAL;
	}

	//status pointer alignment check
	if ((int)statusp % 4 != 0) {
		return EFAULT;
	}
#endif /*PROJECT*/
	s = proc_wait(p);
	if (statusp != NULL)
		*(int *)statusp = s;

	return pid;
#else
	(void)options; /* not handled */
	(void)pid;
	(void)statusp;
	return -1;
#endif
}

/***************************************SYS_GETPID**************************************************/
pid_t sys_getpid(void)
{
	pid_t pid;
#if OPT_WAITPID
	KASSERT(curproc != NULL);
	PROC_LOCK(curproc);
	pid = curproc->p_pid;
	PROC_UNLOCK(curproc);
	return pid;
#else
	return -1;
#endif
}

#if OPT_FORK
/*this is the child's fork entry function*/
static void
call_enter_forked_process(void *tfv, unsigned long dummy)
{
	struct trapframe *tf = (struct trapframe *)tfv;
	(void)dummy;
	enter_forked_process(tf);
	/*enter_forked_process is defined in syscall.c*/

	panic("enter_forked_process returned (should not happen)\n");
}

/*nella sys_fork entra solo il padre*/

/***************************************SYS_FORK****************************************************/
/*nella sys_fork entra solo il padre*/
int sys_fork(struct trapframe *ctf, pid_t *retval)
{
	struct trapframe *tf_child;
	struct proc *newp;
	int result;
	int new_pid = 0;
	struct proc *parent = curproc;
	struct thread *thread = curthread;

	KASSERT(curproc != NULL); /*curproc sarebbe il padre*/
	KASSERT(thread != NULL);

	/*TODO: check if there are already too many processes on the system*/
	if (false) {
		return ENPROC;
	}

	/*check if the current user already has too many processes*/
	if (false) {
		return ENPROC;
	}

#if 0
  kprintf("Parent process: address %p name %s pid %d\n", parent, parent->p_name, parent->p_pid);
#endif
	/*create child process*/
	newp = proc_create_runprogram(parent->p_name);
	if (newp == NULL) {
		return ENOMEM;
	}
	new_pid = newp->p_pid;
	/*check if generated pid is valid*/
	if (new_pid < PID_MIN || new_pid > PID_MAX) {
		proc_destroy(newp);
		return EINVAL;
	}

	/*then we have to copy parent's address space*/
	tf_child = kmalloc(sizeof(struct trapframe)); /*allocation*/
	if (tf_child == NULL) {
		proc_destroy(newp);
		return ENOMEM;
	}

	/*copy parent's trap frame and pass it to child thread*/
	memcpy(tf_child, ctf, sizeof(struct trapframe));
	result = as_copy(curproc->p_addrspace, &(newp->p_addrspace));
	if (result || newp->p_addrspace == NULL) {
		proc_destroy(newp);
		return -result;
	}

	/*linking parent/child, so that child terminated 
     on parent exit */
	newp->p_parent = parent; /*il figlio punta al padre*/
	proc_add_child(parent, newp); /*aggiungo il figlio alla lista dei processi figli del padre*/
	/*qui fa partire il figlio*/
	result = thread_fork(
		curthread->t_name, newp,
		call_enter_forked_process,
		(void *)tf_child, (unsigned long)0 /*unused*/);

	if (result) {
		proc_destroy(newp);
		kfree(tf_child);
		return ENOMEM;
	}

	*retval = newp->p_pid; /*parent returns with child's pid immediately*/
	return 0;
}
#endif /*OPT_FORK*/

