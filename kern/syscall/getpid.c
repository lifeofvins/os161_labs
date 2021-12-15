/*
 * AUthor: G.Cabodi
 * Implementation of sys_getpid
 */

#include <types.h>
#include <kern/unistd.h>
#include <syscall.h>
#include <lib.h>
#include <proc.h>
#include <thread.h>
#include <current.h>
#include <synch.h>


pid_t sys_getpid(void)
{
	pid_t pid;
#if OPT_WAITPID
	KASSERT(curproc != NULL);
	pid = curproc->p_pid;

	return pid;
#else
	return -1;
#endif
}



