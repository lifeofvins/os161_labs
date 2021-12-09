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

static char **kargs;
static unsigned char kargbuf[ARG_MAX];

#define MAX_PROG_NAME 32

static void kfree_args(void **args)
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

/**
 * return the nearest length aligned to alignment.
 */
static int
get_aligned_length(char *arg, int alignment)
{
	int len = 0;

	while (arg[len] != '\0')
		len++;
	len++; /*'\0' character*/
	if (len % 4 == 0)
		return len;

	return len + (alignment - (len % alignment)); //padding
}

static int
copy_args(char **args, int *argc, int *buflen)
{
	int i = 0;
	int err;
	int nlast = 0;
	unsigned char *p_begin = NULL;
	unsigned char *p_end = NULL;
	uint32_t offset;
	uint32_t last_offset;
	int len;

	//initialize the number of arguments and the buffer size
	*argc = 0;
	*buflen = 0;

	/*count number of arguments*/
	while (args[*argc] != NULL)
	{
		*argc++;
	}
		KASSERT(argc != NULL);
	if (*argc >= ARG_MAX)
	{
		return E2BIG;
	}
	int dim = *argc; /*for more readability*/
	kargs = (char **)kmalloc(dim * sizeof(char **));
	if (kargs == NULL)
	{
		return ENOMEM;
	}

	//copy-in kargs.
	for (i = 0; i < dim; i++)
	{
		len = strlen(args[i]) + 1;
		kargs[i] = (char *)kmalloc(len * sizeof(char *));
		if (kargs[i] == NULL)
		{
			kfree_args((void **)kargs);
			return ENOMEM;
		}
		err = copyinstr((userptr_t)args[i], kargs[i], len, NULL);
		if (err)
		{
			kfree_args((void **)kargs);
			return err;
		}
		/*align by 4 for buffer*/
		*buflen += get_aligned_length(kargs[i], 4) + sizeof(char *);
	}

	//if there is a problem, and we haven't read a single argument
	//that means the given user argument pointer is invalid.
	if (i == 0 && err)
		return err;

	//the last argument is NULL
	*argc += 1;
	*buflen += sizeof(char *);

	//loop over the arguments again, building karbuf.
	p_begin = kargbuf;
	p_end = kargbuf + (*argc * sizeof(char *));
	nlast = 0;
	last_offset = *argc * sizeof(char *);
	for (i = 0; i < *argc; i++)
	{
		offset = last_offset + nlast;
		nlast = get_aligned_length(kargs[i], 4);

		//copy the integer into 4 bytes.
		*p_begin = offset & 0xff;
		*(p_begin + 1) = (offset >> 8) & 0xff;
		*(p_begin + 2) = (offset >> 16) & 0xff;
		*(p_begin + 3) = (offset >> 24) & 0xff;

		//copy the string the buffer.
		memcpy(p_end, kargs, nlast);
		p_end += nlast;

		//advance p_begin by 4 bytes.
		p_begin += 4;

		//adjust last offset
		last_offset = offset;
	}

	//set the NULL pointer (i.e., it takes 4 zero bytes.)
	*p_begin = 0;
	*(p_begin + 1) = 0;
	*(p_begin + 2) = 0;
	*(p_begin + 3) = 0;

	return 0;
}

static int
adjust_kargsbuf(int nparams, vaddr_t stack_ptr)
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
		new_offset = stack_ptr + old_offset;

		//store it instead of the old one.
		memcpy(kargbuf + index, &new_offset, sizeof(int));
	}

	return 0;
}

int sys_execv(char *progname, char **args)
{
	struct addrspace *newas = NULL;
	struct addrspace *oldas = NULL;
	struct vnode *vn = NULL;
	vaddr_t entry_ptr;
	vaddr_t stack_ptr;
	int err;
	char *kprogram;
	int argc;
	int buflen; /*how much does the stackptr have to shift*/
	int len;

	KASSERT(curproc != NULL);
	/*parameters check*/
	if (progname == NULL || args == NULL)
	{
		return EFAULT;
	}
	//lock the execv args
	lock_acquire(lk_exec);

	//copy the old addrspace just in case.
	oldas = curproc->p_addrspace;

	//copyin the program name.
	len = strlen(progname) + 1;
	kprogram = kmalloc(len);
	if (kprogram == NULL)
	{
		return ENOMEM;
	}
	err = copyinstr((const_userptr_t)progname, kprogram, len, NULL);
	if (err)
	{
		kfree_args((void **)kargs);
		kfree(kprogram);
		lock_release(lk_exec);
		return err;
	}

	//try to open the given executable.
	err = vfs_open(kprogram, O_RDONLY, 0, &vn);
	if (err)
	{
		lock_release(lk_exec);
		return err;
	}

	//copy the arguments into the kernel buffer
	//and build the user buffer (&buflen)to copy into user stack.
	err = copy_args(args, &argc, &buflen);
	if (err)
	{
		lock_release(lk_exec);
		vfs_close(vn);
		return err;
	}

	//create the new addrspace.
	newas = as_create();
	if (newas == NULL)
	{
		lock_release(lk_exec);
		vfs_close(vn);
		return ENOMEM;
	}

	//activate the new addrspace.
	as_activate();

	//temporarily switch the addrspaces.
	curproc->p_addrspace = newas;

	//load the elf executable.
	err = load_elf(vn, &entry_ptr);
	if (err)
	{
		curproc->p_addrspace = oldas;
		as_activate();

		as_destroy(newas);
		vfs_close(vn);
		lock_release(lk_exec);
		return err;
	}

	//create a stack for the new addrspace.
	err = as_define_stack(newas, &stack_ptr);
	if (err)
	{
		curproc->p_addrspace = oldas;
		as_activate();

		as_destroy(newas);
		vfs_close(vn);
		lock_release(lk_exec);
		return err;
	}

	//adjust the stackptr to reflect the change
	stack_ptr -= buflen;
	err = adjust_kargsbuf(argc, stack_ptr);
	if (err)
	{
		curproc->p_addrspace = oldas;
		as_activate();

		as_destroy(newas);
		vfs_close(vn);
		lock_release(lk_exec);
		return err;
	}

	//copy the arguments into the new user stack.
	err = copyout(kargbuf, (userptr_t)stack_ptr, buflen);
	if (err)
	{
		curproc->p_addrspace = oldas;
		as_activate();
		as_destroy(newas);
		vfs_close(vn);
		lock_release(lk_exec);
		return err;
	}

	//reelase lk_exec
	lock_release(lk_exec);

	//no need for it anymore.
	vfs_close(vn);

	//we are good to go.
	as_destroy(oldas);

	//off we go to userland.
	enter_new_process(argc - 1, (userptr_t)stack_ptr, NULL, stack_ptr, entry_ptr);

	panic("execv: we should not be here.");
	return EINVAL;
}