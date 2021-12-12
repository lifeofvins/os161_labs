/*
 * Author: Grazia D'Onghia
 * Implementation of sys_waitpid, starting from 
 * system and device programming labs about OS161
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <syscall.h>
#include <lib.h>
#include <proc.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <kern/wait.h>

int sys_waitpid(pid_t pid, userptr_t statusp, int options, pid_t *retval)
{

#if OPT_WAITPID

    KASSERT(curthread != NULL);
    KASSERT(curproc != NULL);

    /*status pointer check invalid alignment*/
    if ((unsigned int)statusp & 0x3)
    {
        *retval = -1;
        return EFAULT;
    }
    /*null status pointer*/
    if ((void *)statusp == NULL) {
        *retval = -1;
        return EINVAL;
    }
    /*status must point to userland*/
    if ((void *)statusp >= KERNEL_PTR) {
        *retval = -1;
        return EFAULT;
    }

    /*invalid ptr*/
    if ((void *)statusp == INVALID_PTR) {
        *retval = -1;
        return EINVAL;
    }
    /*options check*/
    if (options != 0 && options != WNOHANG)
    {
        *retval = -1;
        return EINVAL;
    }
    //invalid pid check
    if (pid <= 0)
    {
        *retval = -1;
        return EINVAL;
    }
    //get the process associated with the given pid
    struct proc *p = proc_search_pid(pid, retval);
    int s;

    if (p == NULL)
    {
        //the pid doesn't exist
        *retval = -1;
        return ESRCH; //the pid argument named a nonexistent process
    }
    if (pid > PID_MAX || pid < PID_MIN)
    {
        *retval = -1;
        return EINVAL;
    }

    //if the pid exists, are we allowed to wait for it? i.e, is it our child?
    if (curproc != p->p_parent)
    {
        *retval = -1;
        return ECHILD;
    }
    //sys_waitpid returns error if the calling process doesn't have any child
    if (curproc->p_children->num == 0)
    {
        *retval = -1;
        return ECHILD;
    }

    //if WNOHANG was given, and said process is not yet dead, we immediately return 0
    if (options == WNOHANG && !p->p_exited)
    {
        *retval = 0;
        return 0;
    }
    s = proc_wait(p);
    *(int *)statusp = s;

    return pid;
#else
    (void)options; /* not handled */
    (void)pid;
    (void)statusp;
    return -1;
#endif
}
