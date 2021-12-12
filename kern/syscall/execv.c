/*
 * Author: Grazia D'Onghia
 * Implementation of sys_execv
 */

#include <types.h>
#include <lib.h>
#include <copyinout.h>
#include <proc.h>
#include <thread.h>
#include <current.h>
#include <addrspace.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <kern/errno.h>
#include <machine/trapframe.h>
#include <synch.h>
#include <vfs.h>
#include <syscall.h>

/*Shared data structures --> we need a lock for them (exec_lock)*/
static char karg[ARG_MAX];			   /*argument string, it's not a string array*/
static unsigned char kargbuf[ARG_MAX]; /*array of bytes*/

#define MAX_PROG_NAME 32

/*
 * This function returns the required padding for user args
 */
static int
padded_length(char *arg, int alignment)
{
	int len = strlen(arg) + 1;

	if (len % 4 == 0)
		return len;

	return len + (alignment - (len % alignment));
}

/*
 * This function copies the args from user space to kernel space, 
 * building the kernel buffer, which will be then used to store args pointers
 * for user stack
 */
static int
copy_args_to_kbuf(char **args, int *argc, int *buflen)
{
	int i;
	int err;
	int padding = 0; /*last argument*/
	unsigned char *p_begin = NULL;
	unsigned char *p_end = NULL;
	volatile int offset; //volatile so it isn't optimized out during debug
	int last_offset;

	//initialize the number of arguments and the buffer size
	*argc = 0;

	*buflen = 0;
	/*find how many arguments*/
	for (i = 0; args[i] != NULL; i++)
		;
	*argc = i + 1; //count also the last NULL argument

	/*initialize kernel buffer*/
	p_begin = kargbuf; /*they both point to the same memory region*/
	last_offset = *argc * sizeof(char *);
	p_end = p_begin + last_offset;
	padding = 0;
	i = 0;
	while (args[i] != NULL)
	{
		err = copyinstr((userptr_t)args[i], karg, sizeof(karg), NULL);
		if (err)
			return err;

		/*now I have in karg the i-th argument*/
		offset = last_offset + padding;
		padding = padded_length(karg, 4);

		//copy the string the buffer.
		/*
		 *void *memcpy(void *dest, const void *src, size_t len);
		 *copies n characters from memory area src to memory area dest.
		 */
		memcpy(p_end, karg, padding);

		/*save the offset*/
		*p_begin = offset;

		p_end += padding;

		//advance p_begin by 4 bytes --> go to next argument for next iteration.
		p_begin += 4;

		//adjust last offset
		last_offset = offset;

		++i;
		*buflen += padding + sizeof(char *); /*how much will the stackptr have to shift for every arg*/
	}

	*buflen += sizeof(char *);
	return 0;
}

/*
 * This function modifies the kargbuf content in order to store
 * user stack position of the passed arguments
 */
static int
change_kargbuf_for_userstack(int nparams, vaddr_t stackptr)
{
	int i;
	int new_offset = 0;
	int old_offset = 0;
	int index;

	for (i = 0; i < nparams - 1; ++i)
	{
		index = i * sizeof(char *); //position of the i-th argument
		//read the old offset.
		old_offset = kargbuf[index];

		//calculate the new offset
		new_offset = stackptr + old_offset;

		//store it instead of the old one.
		memcpy(kargbuf + index, &new_offset, sizeof(int));
	}

	return 0;
}

/**
 * Implementation of the execv system call.
 * 
 * Parameters:
 * - program: string with user program name
 * - args: array of args
 */
int sys_execv(char *program, char **args)
{
	struct addrspace *newas;
	struct addrspace *oldas;
	struct vnode *vn;
	vaddr_t entrypoint;
	vaddr_t stackptr;
	int err;
	char *kprogram;
	int argc;
	int buflen;
	int len;

	//preliminar checks
	if (curproc == NULL)
	{
		return ESRCH; //no such process
	}
	if (program == NULL || args == NULL)
	{
		return EFAULT;
	}

	if ((void *)program == INVALID_PTR || (void *)args == INVALID_PTR)
	{
		return EINVAL;
	}
	if ((void *)program >= KERNEL_PTR || (void *)args >= KERNEL_PTR)
	{
		return EINVAL;
	}
	lock_acquire(exec_lock);

	//copy the arguments into the kernel buffer.
	err = copy_args_to_kbuf(args, &argc, &buflen);
	if (err)
	{
		lock_release(exec_lock);
		vfs_close(vn);
		return err;
	}
	//copyin the program name.
	len = strlen(program) + 1;
	kprogram = kmalloc(len);
	if (kprogram == NULL)
	{
		lock_release(exec_lock);
		return ENOMEM;
	}
	err = copyinstr((userptr_t)program, kprogram, len, NULL);
	if (err)
	{
		lock_release(exec_lock);
		return err;
	}

	//open the given executable.
	err = vfs_open(kprogram, O_RDONLY, 0, &vn);
	if (err)
	{
		lock_release(exec_lock);
		return err;
	}

	//create the new addrspace.
	newas = as_create();
	if (newas == NULL)
	{
		lock_release(exec_lock);
		vfs_close(vn);
		return ENOMEM;
	}

	//activate the new addrspace.
	oldas = proc_setas(newas); //proc_setas returns the old addrspace
	as_activate();

	//load the elf executable.
	err = load_elf(vn, &entrypoint);
	if (err)
	{
		proc_setas(oldas);
		as_activate();

		as_destroy(newas);
		vfs_close(vn);
		lock_release(exec_lock);
		return err;
	}

	//create a stack for the new addrspace.
	err = as_define_stack(newas, &stackptr);
	if (err)
	{
		proc_setas(oldas);
		as_activate();

		as_destroy(newas);
		vfs_close(vn);
		lock_release(exec_lock);
		return err;
	}

	//adjust the stackptr to reflect the change
	/*stackptr starts from 0x80000000,
	 *i.e if I lower by buflen I go to the user stack
	 */
	stackptr -= buflen;
	err = change_kargbuf_for_userstack(argc, stackptr);
	if (err)
	{
		proc_setas(oldas);
		as_activate();

		as_destroy(newas);
		vfs_close(vn);
		lock_release(exec_lock);
		return err;
	}

	//copy the arguments into the new user stack.
	err = copyout((void *)kargbuf, (userptr_t)stackptr, buflen);
	if (err)
	{
		proc_setas(oldas);
		as_activate();
		as_destroy(newas);
		vfs_close(vn);
		lock_release(exec_lock);
		return err;
	}

	lock_release(exec_lock);

	vfs_close(vn);

	as_destroy(oldas);

	enter_new_process(argc - 1, (userptr_t)stackptr, NULL, stackptr, entrypoint);

	panic("execv: we should not be here.");
	return EINVAL;
}