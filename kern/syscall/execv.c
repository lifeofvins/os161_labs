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

/*at the beginning it's '\000' repeated ARG_MAX times*/
static char karg[ARG_MAX];			   /*argument string, it's not a string array*/
static unsigned char kargbuf[ARG_MAX]; /*array of bytes for user stack*/

#define MAX_PROG_NAME 32

/**
 * return the nearest length aligned to alignment.
 */
static int
padded_length(char *arg, int alignment)
{
	int len = strlen(arg) + 1;

	if (len % 4 == 0)
		return len;

	return len + (alignment - (len % alignment));
}

static int
copy_args(char **args, int *argc, int *buflen)
{
	int i;
	int err;
	int nlast = 0; /*last argument*/
	unsigned char *p_begin = NULL;
	unsigned char *p_end = NULL;
	int offset;
	int last_offset;

	//initialize the number of arguments and the buffer size
	*argc = 0;

	*buflen = 0;
	/*find how many arguments*/
	for (i = 0; args[i] != NULL; i++)
		;
	*argc = i + 1; //count also the last NULL argument

	/*initialize kernel buffer*/
	p_begin = kargbuf;
	last_offset = *argc * sizeof(char *);
	p_end = p_begin + last_offset;
	nlast = 0;
	i = 0;
	while (args[i] != NULL)
	{
		err = copyinstr((userptr_t)args[i], karg, sizeof(karg), NULL);
		if (err)
			return err;

		/*now I have in karg the i-th argument*/
		offset = last_offset + nlast;
		nlast = padded_length(karg, 4);

		/*convert int to 4 bytes*/
		*p_begin = offset & 0xff; /*first element*/
		*(p_begin + 1) = (offset >> 8) & 0xff;
		*(p_begin + 2) = (offset >> 16) & 0xff;
		*(p_begin + 3) = (offset >> 24) & 0xff;

		//copy the string the buffer.
		/*
		 *void *memcpy(void *dest, const void *src, size_t len);
		 *copies n characters from memory area src to memory area dest.
		 */
		memcpy(p_end, karg, nlast);
		p_end += nlast;

		//advance p_begin by 4 bytes --> go to next argument for next iteration.
		p_begin += 4;

		//adjust last offset
		last_offset = offset;

		++i;
		*buflen += padded_length(karg, 4) + sizeof(char *); /*how much will the stackptr have to shift for every arg*/
	}

	*buflen += sizeof(char *);
	return 0;
}

static int
adjust_kargbuf(int nparams, vaddr_t stackptr)
{
	int i;
	uint32_t new_offset = 0;
	uint32_t old_offset = 0;
	int index;

	for (i = 0; i < nparams - 1; ++i)
	{
		index = i * sizeof(char *);
		//read the old offset.
		old_offset = ((0xFF & kargbuf[index + 3]) << 24) | ((0xFF & kargbuf[index + 2]) << 16) |
					 ((0xFF & kargbuf[index + 1]) << 8) | (0xFF & kargbuf[index]);

		//calculate the new offset
		new_offset = stackptr + old_offset;

		//store it instead of the old one.
		memcpy(kargbuf + index, &new_offset, sizeof(int));
	}

	return 0;
}

int sys_execv(char *program, char **args)
{
	struct addrspace *newas = NULL;
	struct addrspace *oldas = NULL;
	struct vnode *vn = NULL;
	vaddr_t entrypoint;
	vaddr_t stackptr;
	int err;
	char *kprogram;
	int argc;
	int buflen;
	int len;

	KASSERT(curproc != NULL);
	/*check if arguments are valid*/
	if (program == NULL || args == NULL)
	{
		return EFAULT;
	}
	lock_acquire(exec_lock);

	//copy the arguments into the kernel buffer.
	err = copy_args(args, &argc, &buflen);
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

	//try to open the given executable.
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
		curproc->p_addrspace = oldas;
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
	stackptr -= buflen;
	err = adjust_kargbuf(argc, stackptr);
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
	err = copyout(kargbuf, (userptr_t)stackptr, buflen);
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