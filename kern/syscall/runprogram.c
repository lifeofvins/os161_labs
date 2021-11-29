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

#include "opt-file.h"
#include "opt-execv.h"




#if OPT_EXECV

/*SDP OS161 PROJECT*/


static
void kfree_args(void **args)
{
	/*this function frees all the string arrays allocated during execv*/
	int i = 0;
	while (args[i] != NULL)
	{
		args[i] = NULL;
		kfree(args[i]);
		i++;
	}
	kfree(args[i]); //NULL value
	kfree(args);
	return;
}

/***************************************SYS_EXECV***************************************************/
int sys_execv(char *program, char **args)
{
	
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	size_t argc, len, waste;
	int i;

	/*strutture kernel*/
	char *kprogram;
	char **kargs;

	struct addrspace *oldas;
	struct addrspace *newas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL); //warning

	/*verifico che entrambi gli argomenti passati ad execv siano puntatori validi*/
	if (program == NULL || args == NULL)
	{
		return EFAULT;
	}

	/*trovo la dimensione del vettore: ciclo finchè non trovo un puntatore a NULL*/

	for (argc = 0; args[argc] != NULL; argc++)
		;

	if (argc >= ARG_MAX)
	{
		return E2BIG;
	}

	/*alloco il vettore*/
	kargs = (char **)kmalloc(argc * sizeof(char **));
	if (kargs == NULL)
	{
		return ENOMEM;
	}

	/*alloco ogni riga del vettore*/
	for (i = 0; i < (int)argc; i++)
	{
		len = strlen(args[i]) + 1;
		kargs[i] = (char *)kmalloc(len * sizeof(char *));
		if (kargs[i] == NULL)
		{
			kfree_args((void **)kargs);
			return ENOMEM;
		}
		/*una volta allocato l'elemento i-esimo del vettore facico copyin da user a kernel*/
		result = copyinstr((const_userptr_t)args[i], kargs[i], len, &waste);
		if (result)
		{
			kfree_args((void **)kargs);
			return -result;
		}
	}

	kargs[i] = NULL;
	/*faccio la stessa cosa col nome del programma*/
	len = strlen(program) + 1;
	kprogram = kmalloc(len);
	if (kprogram == NULL)
	{
		kfree(kprogram);
		return ENOMEM;
	}
	result = copyinstr((const_userptr_t)program, kprogram, len, &waste);
	if (result)
	{
		kfree_args((void **)kargs);
		kfree(kprogram);
		return -result;
	}
	/* Open the file. */
	result = vfs_open(kprogram, O_RDONLY, 0, &v);
	if (result)
	{
		as_destroy(curproc->p_addrspace);
		kfree_args((void **)kargs);
		kfree(kprogram);
		return -result;
	}
	/*address space handling*/

	/*create a new address space*/
	newas = as_create();
	if (newas == NULL)
	{
		vfs_close(v);
		return ENOMEM;
	}

	/*Switch to it and activate it*/
	oldas = proc_setas(newas); //proc_setas ritorna il vecchio as
	as_activate();

	/*Load the executable*/
	result = load_elf(v, &entrypoint);
	if (result)
	{
		/*p_addrspace will go away when curproc is destroyed*/
		vfs_close(v);

		return result;
	}

	/* Done with the file now. */
	vfs_close(v);
	/* Define the user stack in the address space */
	result = as_define_stack(newas, &stackptr);
	if (result)
	{
		/* p_addrspace will go away when curproc is destroyed */
		return -result;
	}

	/*devo tornare da kernel a user: qui devo usare il padding*/
	/*strutture user dopo kernel*/
	char **uargs;
	int length;

	uargs = (char **)kmalloc(sizeof(char *) * (argc + 1));
	if (uargs == NULL)
	{
		as_destroy(curproc->p_addrspace);
		kfree_args((void **)kargs);
		kfree(kprogram);

		return ENOMEM;
	}
	for (i = 0; i < (int)argc; i++)
	{
		length = strlen(kargs[i]) + 1;
		uargs[i] = (char *)kmalloc(length * sizeof(char *));
		stackptr -= length;
		/*padding*/
		if (stackptr & 0x3) stackptr -= stackptr & 0x3; //alignment by 4
		result = copyoutstr(kargs[i], (userptr_t)stackptr, length, &waste); /*copio da kernel a user*/
		if (result)
		{
			kfree_args((void **)kargs);
			kfree(kprogram);
			return -result;
		}

		uargs[i] = (char *)stackptr; /*mi salvo l'indiritto dello stack user in uargs*/
	}

	for (i = 0; i < (int)argc; i++)
	{
		result = copyout((const void *)uargs[argc - (i + 1)], (userptr_t)stackptr, sizeof(char *));
		if (result)
		{
			kfree_args((void **)kargs);
			kfree(kprogram);
			return -result;
		}
	}

	uargs[i] = NULL;
	as_destroy(oldas);

	kfree_args((void **)kargs);
	kfree(kprogram);
	kfree_args((void **)uargs); //non funziona perchè sono indirizzi user e si blocca su KASSERT di kfree perchè non è multiplo di pagina
	/* Warp to user mode. */
	enter_new_process(argc /*argc*/, (userptr_t)stackptr /*userspace addr of argv*/,
					  NULL /*userspace addr of environment*/,
					  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
	return 0;
}
#endif



/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */


 
int
runprogram(char *progname, unsigned long argc, char **args)
{
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

#if OPT_EXECV
	int i;
	size_t len;
	size_t stack_offset = 0;
	char **argvptr;
	vaddr_t uprogname[1];
#endif

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

#if OPT_EXECV
	if (args != NULL) {
		/*program has arguments*/
		argvptr = (char **)kmalloc(argc*sizeof(char **));
		if (argvptr == NULL) {
			return ENOMEM;
		}
		/*last item points to NULL*/
		argvptr[argc] = NULL;

		for (i = 0; i < (int)argc; i++) {
			/*allocation*/
			len = strlen(args[i])+1;
			argvptr[i] = (char *)kmalloc(sizeof(char *));
			if (argvptr[i] == NULL) {
				return ENOMEM;
			}
			stackptr -= len;
			if (stackptr & 0x3) {
				stackptr -= stackptr & 0x3;
			}

			/*copy from kernel buffer to user space*/
			copyoutstr(args[i], (userptr_t)stackptr, len, NULL);
			argvptr[i] = (char *)stackptr; /*save current position in the stack of the argument*/
		}
		argvptr[argc] = 0;
		stack_offset += sizeof(char *)*(argc+1);
		stackptr -= stack_offset;
		result = copyout(argvptr, (userptr_t)stackptr, sizeof(char *)*(argc));
		if (result) {
			return result;
		}
		/* Warp to user mode. */
		enter_new_process(argc, (userptr_t)stackptr /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	}
	else {
		/*we have just the progname*/
		len = strlen(progname)+1;
		uprogname[0] = stackptr - len;
		copyoutstr(progname, (userptr_t)uprogname[0], len, NULL);

		len += sizeof(vaddr_t);
		stackptr = stackptr -len - ((stackptr - len)%8);
		copyout(uprogname, (userptr_t)stackptr, sizeof(vaddr_t));
		/* Warp to user mode. */
		enter_new_process(1, (userptr_t)stackptr,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	}
#else
	/* Warp to user mode. */
	enter_new_process(0, NULL,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);
#endif
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}