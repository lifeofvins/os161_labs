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

static char karg[ARG_MAX]; /*argument string, it's not a string array*/
static unsigned char kargbuf[ARG_MAX];

#define MAX_PROG_NAME 32

/**
 * return the nearest length aligned to alignment.
 */
static int
get_aligned_length(char *arg, int alignment)
{
	int len = strlen(arg)+1;

	if (len % 4 == 0)
		return len;

	return len + (alignment - (len % alignment));
}

static int
copy_args(char **uargs, int *nargs, int *buflen)
{
	int i = 0;
	int err;
	int nlast = 0;
	char *ptr;
	unsigned char *p_begin = NULL;
	unsigned char *p_end = NULL;
	uint32_t offset;
	uint32_t last_offset;

	//check whether we got a valid pointer.
	if (uargs == NULL)
		return EFAULT;

	//initialize the numbe of arguments and the buffer size
	*nargs = 0;
	*buflen = 0;

	//copy-in kargs.
	i = 0;
	while ((err = copyin((userptr_t)uargs + i * 4, &ptr, sizeof(ptr))) == 0)
	{
		if (ptr == NULL)
			break;
		err = copyinstr((userptr_t)ptr, karg, sizeof(karg), NULL);
		if (err)
			return err;

		++i;
		*nargs += 1;
		*buflen += get_aligned_length(karg, 4) + sizeof(char *);
	}

	//if there is a problem, and we haven't read a single argument
	//that means the given user argument pointer is invalid.
	if (i == 0 && err)
		return err;

	//account for NULL also.
	*nargs += 1;
	*buflen += sizeof(char *);

	//loop over the arguments again, building karbuf.
	i = 0;
	p_begin = kargbuf;
	p_end = kargbuf + (*nargs * sizeof(char *));
	nlast = 0;
	last_offset = *nargs * sizeof(char *);
	while ((err = copyin((userptr_t)uargs + i * 4, &ptr, sizeof(ptr))) == 0)
	{
		if (ptr == NULL)
			break;
		err = copyinstr((userptr_t)ptr, karg, sizeof(karg), NULL);
		if (err)
			return err;

		offset = last_offset + nlast;
		nlast = get_aligned_length(karg, 4);
		//copy the integer into 4 bytes.
		*p_begin = offset & 0xff;
		*(p_begin + 1) = (offset >> 8) & 0xff;
		*(p_begin + 2) = (offset >> 16) & 0xff;
		*(p_begin + 3) = (offset >> 24) & 0xff;

		//copy the string the buffer.
		memcpy(p_end, karg, nlast);
		p_end += nlast;

		//advance p_begin by 4 bytes.
		p_begin += 4;

		//adjust last offset
		last_offset = offset;
		++i;
	}

	//set the NULL pointer (i.e., it takes 4 zero bytes.)
	*p_begin = 0;
	*(p_begin + 1) = 0;
	*(p_begin + 2) = 0;
	*(p_begin + 3) = 0;

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

int sys_execv(char *upname, char **uargs)
{
	struct addrspace *newas = NULL;
	struct addrspace *oldas = NULL;
	struct vnode *vn = NULL;
	vaddr_t entrypoint;
	vaddr_t stackptr;
	int err;
	char kpname[MAX_PROG_NAME];
	int nargs;
	int buflen;

	KASSERT(curproc != NULL);
	/*check if arguments are valid*/
	if (upname == NULL || uargs == NULL)
	{
		return EFAULT;
	}
	lock_acquire(exec_lock);

	//copy the arguments into the kernel buffer.
	err = copy_args(uargs, &nargs, &buflen);
	if (err)
	{
		lock_release(exec_lock);
		vfs_close(vn);
		return err;
	}
	//copyin the program name.
	err = copyinstr((userptr_t)upname, kpname, sizeof(kpname), NULL);
	if (err)
	{
		lock_release(exec_lock);
		return err;
	}

	//try to open the given executable.
	err = vfs_open(kpname, O_RDONLY, 0, &vn);
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

	//temporarily switch the addrspaces.
	curproc->p_addrspace = newas;

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
		curproc->p_addrspace = oldas;
		as_activate();

		as_destroy(newas);
		vfs_close(vn);
		lock_release(exec_lock);
		return err;
	}

	//adjust the stackptr to reflect the change
	stackptr -= buflen;
	err = adjust_kargbuf(nargs, stackptr);
	if (err)
	{
		curproc->p_addrspace = oldas;
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
		curproc->p_addrspace = oldas;
		as_activate();
		as_destroy(newas);
		vfs_close(vn);
		lock_release(exec_lock);
		return err;
	}

	//reelase exec_lock
	lock_release(exec_lock);

	//no need for it anymore.
	vfs_close(vn);

	//we are good to go.
	as_destroy(oldas);

	//off we go to userland.
	enter_new_process(nargs - 1, (userptr_t)stackptr, NULL, stackptr, entrypoint);

	panic("execv: we should not be here.");
	return EINVAL;
}