/*
 * Author: Grazia D'Onghia
 * Implementation of sys__exit.
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
#include <current.h>
#include <synch.h>
#include <spl.h>
#include <vnode.h>
#include <vfs.h>
#include <test.h>
#include <kern/wait.h>

static int 
close_all_files(struct proc *p) {
    int i, err;
    for (i = 0; i < OPEN_MAX; i++)
    {
        if (p->fileTable[i] != NULL)
        {
            err = sys_close(i);
            if (err) return -1;
        }
    }
    return 0;
}
void sys__exit(int status)
{
#if OPT_WAITPID
    struct proc *p = curproc;
    struct proc *childp = NULL;
    int err;
    KASSERT(childp == NULL);

    //close all open files
    err = close_all_files(p);
    if (err) {
        panic("Problem closing open file of curproc.\n");
    }
    PROC_LOCK(p);
    p->p_status = status & 0xff; /* just lower 8 bits returned */

    proc_remthread(curthread);
#if USE_SEMAPHORE_FOR_WAITPID
    p->p_exited = true;
    V(p->p_sem);
#else
    lock_acquire(p->p_cv_lock);
    p->p_exited = true; /*condition*/
    cv_signal(p->p_cv, p->p_cv_lock);
    lock_release(p->p_cv_lock);
#endif /*SEMAPHORE*/
#else
    /* get address space of current process and destroy */
    struct addrspace *as = proc_getas();
    as_destroy(as);
#endif /*OPT_WAITPID*/
    PROC_UNLOCK(p);
    thread_exit();

    panic("thread_exit returned (should not happen)\n");
    (void)status;
}