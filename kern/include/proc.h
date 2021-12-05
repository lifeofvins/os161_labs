/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _PROC_H_
#define _PROC_H_

/*
 * Definition of a process.
 *
 * Note: curproc is defined by <current.h>.
 */

#include <spinlock.h>
#include <limits.h>
#include <current.h>
#include "opt-waitpid.h"
#include "opt-file.h"
#include "opt-fork.h"
#include <array.h>

struct addrspace;
struct thread;
struct vnode;

/*
 * Process structure.
 *
 * Note that we only count the number of threads in each process.
 * (And, unless you implement multithreaded user processes, this
 * number will not exceed 1 except in kproc.) If you want to know
 * exactly which threads are in the process, e.g. for debugging, add
 * an array and a sleeplock to protect it. (You can't use a spinlock
 * to protect an array because arrays need to be able to call
 * kmalloc.)
 *
 * You will most likely be adding stuff to this structure, so you may
 * find you need a sleeplock in here for other reasons as well.
 * However, note that p_addrspace must be protected by a spinlock:
 * thread_switch needs to be able to fetch the current address space
 * without sleeping.
 */

#if OPT_WAITPID
/* G.Cabodi - 2019 - implement waitpid: 
   synch with semaphore (1) or cond.var.(0) */
#define USE_SEMAPHORE_FOR_WAITPID 0
#endif


struct proc {
	char *p_name;			/* Name of this process */
	struct spinlock p_spinlock;		/* Lock for this structure */
	unsigned p_numthreads;		/* Number of threads in this process */

	/* VM */
	struct addrspace *p_addrspace;	/* virtual address space */

	/* VFS */
	struct vnode *p_cwd;		/* current working directory */

	/* add more material here as needed */
#if OPT_WAITPID
        /* G.Cabodi - 2019 - implement waitpid: synchro, and exit status */
        int p_status;                   /* status as obtained by exit() */
        pid_t p_pid;                    /* process pid */
        
        /*sdp project: sys__exit improvement*/
        bool p_exited; /*is the process exited?*/
        struct lock *p_lock;
#if USE_SEMAPHORE_FOR_WAITPID
	struct semaphore *p_sem;
#else
        struct cv *p_cv;
        struct lock *p_cv_lock;
#endif
#endif

#if OPT_FILE
	/*cabodi*/
	struct openfile *fileTable[OPEN_MAX];

#endif

/*non funziona se metto #if oPT_FORK*/
#if OPT_FORK
	struct proc *p_parent; /*puntatore al padre*/
	struct array *p_children; /*array dei figli*/
#endif
};

/* This is the process structure for the kernel and for kernel-only threads. */
extern struct proc *kproc;

/* Call once during system startup to allocate data structures. */
void proc_bootstrap(void);

/* Create a fresh process for use by runprogram(). */
struct proc *proc_create_runprogram(const char *name);

/* Destroy a process. */
void proc_destroy(struct proc *proc);

/* Attach a thread to a process. Must not already have a process. */
int proc_addthread(struct proc *proc, struct thread *t);

/* Detach a thread from its process. */
void proc_remthread(struct thread *t);

/* Fetch the address space of the current process. */
struct addrspace *proc_getas(void);

/* Change the address space of the current process, and return the old one. */
struct addrspace *proc_setas(struct addrspace *);

/* wait for process termination, and return exit status */
int proc_wait(struct proc *proc);
/* get proc from pid */
struct proc *proc_search_pid(pid_t pid, pid_t *retval);

/* signal end/exit of process */
void proc_signal_end(struct proc *proc);
/*cabodi*/
#if OPT_FILE
void proc_file_table_copy(struct proc *psrc, struct proc *pdest);
#endif

#if OPT_FORK
void proc_add_child(struct proc *parent, struct proc *child);
#endif


//#if OPT_EXECV
struct addrspace *curproc_getas(void);
struct addrspace *curproc_setas(struct addrspace *newas);
//#endif

#define PROC_LOCK(x) (lock_acquire( (x) -> p_lock))
#define PROC_UNLOCK(x) (lock_release( (x) -> p_lock))
#endif /* _PROC_H_ */
