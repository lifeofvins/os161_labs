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

static int
check_statusp(userptr_t statusp) {
/*status pointer check invalid alignment*/
    if ((int)statusp & 0x3) return -1;
    /*null status pointer*/
    if (statusp == NULL) return -1;
    /*status must point to userland*/
    if ((unsigned int)statusp >= MIPS_KSEG0) return -1;

    return 0;
}
int sys_waitpid(pid_t pid, userptr_t statusp, int options, pid_t *retval)
{

#if OPT_WAITPID

    KASSERT(curthread != NULL);
    KASSERT(curproc != NULL);

    int err;

    err = check_statusp(statusp);
    if (err) {
        *retval = err;
        return EFAULT;
    }
    /*options check*/
    if (options != 0 && options != WNOHANG)
    {
        *retval = -1;
        return EINVAL;
    }
    //invalid pid check
    if (pid <= 0) {
        *retval = -1;
        return EINVAL;
    }
    //get the process associated with the given pid
    struct proc *p = proc_search_pid(pid, retval);
    int s;

    if (p == NULL )
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
