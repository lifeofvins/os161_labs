/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than runprogram() does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <copyinout.h>
#include <synch.h>

#include "opt-file.h"
/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
#define ARGS 1

#if ARGS
struct argvdata {
	char *buffer;
	char *bufend;
	size_t *offsets;
	int nargs;
	struct lock *lock;
};

static struct argvdata argdata;

static 
int
copyout_args(struct argvdata *ad, userptr_t *argv, vaddr_t *stackptr) {
	userptr_t argbase, userargv, arg;
	vaddr_t stack;
	size_t buflen;
	int i, result;

	KASSERT(lock_do_i_hold(ad->lock));

	buflen = ad->bufend - ad->buffer;

	/*begin the stack at the passed in top*/
	stack = *stackptr;

	/*copy the block of strings to the top of the user stack*/

	/*figure out where the strings start*/
	stack -= buflen;

	/*align to sizeof(void *) boundary, this is the argbase*/
	stack -= (stack & (sizeof(void *) - 1));
	argbase = (userptr_t)stack;

	/*now just copyout the whole block of arg strings*/
	result = copyout(ad->buffer, argbase, buflen);
	if (result) {
		return result;
	}

	/*now copyout the argv itself. 
	* the stack pointer is already suitably aligned.
	*allow an extra slot for the NULL that terminates the vector
	*/
	stack -= (ad->nargs + 1)*sizeof(userptr_t);
	userargv = (userptr_t)stack;

	for (i = 0; i < ad->nargs; i++) {
		arg = argbase + ad->offsets[i];
		result = copyout(&arg, userargv, sizeof(userptr_t));
		if (result) {
			return result;
		}
		userargv += sizeof(userptr_t);
	}

	/*NULL terminates it*/
	arg = NULL;
	result = copyout(&arg, userargv, sizeof(userptr_t));
	if (result) {
		return result;
	}
	*argv = (userptr_t)stack;
	*stackptr = stack;
	return 0;
}
#endif /*ARGS*/
int
runprogram(char *progname)
{
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
#if ARGS
	int argc; /*number of arguments*/
	userptr_t argv;
#endif

#if ARGS
	/*pid check*/
	KASSERT(curproc->p_pid >= PID_MIN && curproc->p_pid <= PID_MAX);

	lock_acquire(argdata.lock);

	/*make up argv strings*/
	argdata.offsets = kmalloc(sizeof(size_t));
	if (argdata.offsets == NULL) {
		kfree(argdata.offsets);
		lock_release(argdata.lock);
		return ENOMEM;
	}

	/*copy it in, set the single offset*/
	strcpy(argdata.buffer, progname);
	argdata.bufend = argdata.buffer + (strlen(argdata.buffer)+1);
	argdata.offsets[0] = 0;
	argdata.nargs = 1;

#endif /*ARGS*/
	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new process. */
	KASSERT(proc_getas() == NULL);

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}
#if ARGS
	result = copyout_args(&argdata, &argv, &stackptr);
	if (result) {
		kfree(argdata.buffer);
		kfree(argdata.offsets);
		lock_release(argdata.lock);
	}
	argc = argdata.nargs;

	/*free the space*/
	kfree(argdata.buffer);
	kfree(argdata.offsets);
	
	lock_release(argdata.lock);
#endif
	/* Warp to user mode. */
	enter_new_process(argc /*argc*/, argv /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}
