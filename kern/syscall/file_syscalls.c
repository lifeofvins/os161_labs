#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <clock.h>
#include <syscall.h>
#include <current.h>
#include <lib.h>

#include <copyinout.h>
#include <vnode.h>
#include <vfs.h>
#include <limits.h>
#include <uio.h>
#include <proc.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <synch.h>
#include <kern/fcntl.h>

/*max num of system wide open file*/
#define SYSTEM_OPEN_MAX 10 * OPEN_MAX

#define USE_KERNEL_BUFFER 0

struct openfile
{
	struct vnode *vn; /*pointer to vnode*/
	mode_t mode;	  /*read-only, write-only, read-write*/
	off_t offset;
	int accmode;
	struct lock *file_lock;
	unsigned int ref_count; /*openfile could be shared*/
};

struct openfile systemFileTable[SYSTEM_OPEN_MAX];

/**
 * Checks whether the file descriptor has been really allocated.
 * 
 * Parameter:
 * - fd: file descriptor to be tested
 * 
 * Return value:
 * - 0, if fd is not valid
 * - whatever else otherwise
 */
static int 
is_valid_fd(int fd)
{
	if (fd < 0 || fd > OPEN_MAX)
		return 0;
	if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO)
		return 1;
	return !(curproc->fileTable[fd] == NULL);
}

void openfileIncrRefCount(struct openfile *of)
{
	if (of != NULL)
		of->ref_count++;
}

#if USE_KERNEL_BUFFER
/*per effettuare una read si deve predisporre opportunamente un puntatore a struct uio ku
(che descrive il tipo di i/o da effettuare) e il puntatore al FCB*/
static int file_read(int fd, userptr_t buf_ptr, size_t size, int *err)
{
	struct iovec iov;
	struct uio ku;
	int result, nread;
	struct vnode *vn;
	struct openfile *of;
	void *kbuf; /*kernel buffer*/

	if (!is_valid_fd(fd))
	{
		*err = EBADF;
		return -1;
	}
	of = curproc->fileTable[fd];
	if (of == NULL)
	{
		*err = EBADF;
		return -1;
	}
	vn = of->vn;
	if (vn == NULL)
	{
		*err = EINVAL;
		return -1;
	}
	lock_acquire(of->file_lock);

	if (of->accmode == O_WRONLY)
	{
		lock_release(of->file_lock);
		*err = EBADF;
		 return -1;
	}
	/*allocation of kernel buffer*/
	kbuf = kmalloc(size);

	uio_kinit(&iov, &ku, kbuf, size, of->offset, UIO_READ); /*sets up a uio data structure*/

	if (ku.uio_segflg != UIO_SYSSPACE)
	{
		lock_release(of->file_lock);
		*err = EINVAL;
		return -1;
	}
	result = VOP_READ(vn, &ku);
	if (result)
	{
		lock_release(of->file_lock);
		return result;
	}
	/*i dati vanno a finire in memoria kernel*/
	of->offset = ku.uio_offset;
	nread = size - ku.uio_resid;
	copyout(kbuf, buf_ptr, nread); /*copio nel buffer user nread bytes memorizzati in ku (memoria kernel)*/
	kfree(kbuf);

	lock_release(of->file_lock);

	return nread;
}

static int file_write(int fd, userptr_t buf_ptr, size_t size, int *err)
{
	struct iovec iov;
	struct uio ku;
	int result, nwrite;
	struct vnode *vn;
	struct openfile *of;
	void *kbuf;

	struct proc *cur = curproc;
	KASSERT(cur != NULL);

	if (!is_valid_fd(fd))
	{
		*err = EBADF;
		return -1;
	}
	of = curproc->fileTable[fd];
	KASSERT(of != NULL);
	if (of == NULL)
	{
		*err = EBADF;
		return -1;
	}
	vn = of->vn;
	if (vn == NULL)
	{
		*err = EINVAL;
		return -1;
	}
	lock_acquire(of->file_lock);
	if (of->accmode == O_RDONLY)
	{
		lock_release(of->file_lock);
		*err = EBADF;
		return -1;
	}
	kbuf = kmalloc(size);
	/*siccome devo fare la write metto prima i dati nel buffer e poi scrivo*/
	copyin(buf_ptr, kbuf, size);
	uio_kinit(&iov, &ku, kbuf, size, of->offset, UIO_WRITE);
	if (ku.uio_segflg != UIO_SYSSPACE)
	{
		lock_release(of->file_lock);
		*err = EINVAL;
		return -1; /* ? */
	}
	result = VOP_WRITE(vn, &ku);
	if (result)
	{
		lock_release(of->file_lock);
		return result;
	}
	kfree(kbuf);
	of->offset = ku.uio_offset;
	nwrite = size - ku.uio_resid;

	lock_release(of->file_lock);

	return nwrite;
}

#else /*no kernel buffer*/
static int file_read(int fd, userptr_t buf_ptr, size_t size, int *err)
{
	struct iovec iov;
	struct uio u; /*user*/
	struct vnode *vn;
	struct openfile *of;
	int result, nread;

	if (!is_valid_fd(fd))
	{
		*err = EBADF;
		return -1;
	}
	of = curproc->fileTable[fd];
	lock_acquire(of->file_lock);
	if (of == NULL)
	{
		*err = EBADF;
		return -1;
	}
	vn = of->vn;
	if (vn == NULL)
	{
		*err = EINVAL;
		return -1;
	}

	/*agisco sulla struct io user*/
	iov.iov_ubase = buf_ptr;
	iov.iov_len = size;

	u.uio_iov = &iov; /*struct con l'indirizzo logico e la dimensione*/
	u.uio_iovcnt = 1;
	u.uio_resid = size;
	u.uio_offset = of->offset;
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = curproc->p_addrspace; /*punta all'address space del processo corrente*/

	result = VOP_READ(vn, &u);
	if (result)
		return result;

	of->offset = u.uio_offset;
	lock_release(of->file_lock);
	nread = size - u.uio_resid;

	return nread;
}

static int file_write(int fd, userptr_t buf_ptr, size_t size, int *err)
{
	struct iovec iov;
	struct uio u;
	int result, nwrite;
	struct vnode *vn;
	struct openfile *of;

	if (!is_valid_fd(fd))
	{
		*err = EBADF;
		return -1;
	}
	of = curproc->fileTable[fd];
	KASSERT(of != NULL);
	if (of == NULL)
	{
		*err = EBADF;
		return -1;
	}
	vn = of->vn;
	if (vn == NULL)
	{
		*err = EINVAL;
		return -1;
	}

	lock_acquire(of->file_lock);
	iov.iov_ubase = buf_ptr;
	iov.iov_len = size;

	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = size;
	u.uio_offset = of->offset;
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_WRITE;
	u.uio_space = curproc->p_addrspace;

	result = VOP_WRITE(vn, &u);
	if (result)
		return result;

	of->offset = u.uio_offset;
	lock_release(of->file_lock);
	nwrite = size - u.uio_resid;

	return nwrite;
}

#endif /*use kernel buffer*/

/*file system calls for open/close*/

int sys_open(userptr_t path, int openflags, mode_t mode, int *err)
{
	/*1) opens a file: create an openfile item
*2) obtain vnode from vfs_open()
*3) initialize offset in openfile
return the file descriptor of the openfile item
*/

	int fd; /*file descriptor --> index in the file table and return value*/
	int i;
	struct vnode *v;
	struct openfile *of = NULL; /*create an openfile item*/
	struct stat st;
	char fname[PATH_MAX]; /*filename in kernel*/
	int accmode;		  /*access mode*/
	int result;			  /*result of filetable functions*/

	struct proc *cur = curproc;
	KASSERT(cur != NULL);
	/*path pointer check*/
	if (path == NULL)
	{
		*err = EINVAL;
		return -1;
	}

	/*flag check*/
	accmode = openflags & O_ACCMODE;
	if (accmode != O_RDONLY && accmode != O_WRONLY && accmode != O_RDWR)
	{
		*err = EINVAL;
		return -1;
	}

	/*copy a string from user space to kernel space*/
	result = copyinstr(path, fname, sizeof(fname), NULL);

	if (result)
	{
		return result;
	}

	result = vfs_open((char *)path, openflags, mode, &v); /*obtain vnode from vfs_open()*/
	if (result)
	{
		*err = ENOENT;
		return -1;
	}

	/*search in system open file table*/
	for (i = 0; i < SYSTEM_OPEN_MAX; i++)
	{
		/*search for free pos in which place the openfile struct*/
		if (systemFileTable[i].vn == NULL)
		{
			of = &systemFileTable[i];
			of->vn = v;
			of->offset = 0; /*initialize offset.*/
			of->accmode = accmode;
			of->file_lock = lock_create(fname);
			of->ref_count = 1;
			break;
		}
	}
	if (of->file_lock == NULL)
	{
		vfs_close(v);
		*err = ENOMEM;
		return -1;
	}
	if (of == NULL)
	{
		/*non ho trovato posto nella system open file table*/
		*err = ENFILE;
		lock_destroy(of->file_lock);
	}
	else
	{
		if (openflags & O_APPEND)
		{
			result = VOP_STAT(of->vn, &st);
			if (result)
			{
				vfs_close(v);
				*err = EINVAL;
				return -1;
			}
			of->offset = st.st_size;
		}
		for (fd = STDERR_FILENO + 1; fd < OPEN_MAX; fd++)
		{
			if (curproc->fileTable[fd] == NULL)
			{
				curproc->fileTable[fd] = of;
				return fd;
			}
		}
		/*no free slot in process open file table*/
		*err = EMFILE;
	}
	/*if I'm here, something went wrong*/
	vfs_close(v);
	return -1;
}
int sys_close(int fd, int *err)
{
	struct openfile *of = NULL;
	struct vnode *vn;

	struct proc *cur = curproc;
	KASSERT(cur != NULL);

	/*Incorrect file descriptor*/
	if (!is_valid_fd(fd))
	{
		*err = EBADF;
		return -1;
	}

	of = curproc->fileTable[fd];

	if (of == NULL)
	{
		*err = EBADF;
		return -1;
	}

	curproc->fileTable[fd] = NULL;

	vn = of->vn;
	of->vn = NULL;
	if (vn == NULL)
	{
		*err = EINVAL;
		return -1;
	}

	lock_acquire(of->file_lock);
	/*if it is the last close of this file, free it up*/
	if (of->ref_count == 1)
	{
		vfs_close(vn);
		lock_release(of->file_lock);
		lock_destroy(of->file_lock);
	}
	else
	{
		KASSERT(of->ref_count > 1);
		of->ref_count--;
		lock_release(of->file_lock);
	}

	return 0;
}
/*
 * simple file system calls for write/read
 */
int sys_write(int fd, userptr_t buf_ptr, size_t size, int *err)
{
	int i;
	char *p = (char *)buf_ptr;

	struct proc *cur = curproc;
	KASSERT(cur != NULL);

	if (fd == STDIN_FILENO)
	{
		/*we cannot write on stdin*/
		*err = EINVAL;
		return -1;
	}
	if (fd == STDOUT_FILENO || fd == STDERR_FILENO)
	{
		if (curproc->fileTable[fd] != NULL)
		{
			return file_write(fd, buf_ptr, size, err);
		}
		for (i = 0; i < (int)size; i++)
		{
			putch(p[i]);
		}
	}
	else
	{
		return file_write(fd, buf_ptr, size, err);
	}

	return (int)size;
}
int sys_read(int fd, userptr_t buf_ptr, size_t size, int *err)
{
	char *p = (char *)buf_ptr;
	int i;
	if (fd == STDERR_FILENO)
	{
		*err = EINVAL;
		return -1;
	}
	if (fd != STDIN_FILENO)
	{
		if (curproc->fileTable[fd] != NULL)
		{
			return file_read(fd, buf_ptr, size, err);
		}
		return file_read(fd, buf_ptr, size, err);
	}

	for (i = 0; i < (int)size; i++)
	{
		p[i] = getch();
		if (p[i] < 0)
			return i;
	}

	return (int)size;
}

/**
 * Implementation of the dup2 system call.
 * 
 * It manages the cases in which bad file descriptors
 * are passed as parameters.
 * 
 * Parameters:
 * - old_fd: the old file descriptor
 * - new_fd: the new file descriptor
 * - err: integer pointer set to new_fd if it's all
 *            gone ok, -1 otherwise.
 * 
 * Example of usage:
 *      int logfd = open("logfile", O_WRONLY);
 *      dup2(logfd, STDOUT_FILENO); 
 *      printf("Hello, OS161.\n");
 * 
 * 
 * Return value:
 * 0: on success
 * -1: on failure 
 */
int 
sys_dup2(int old_fd, int new_fd, int *err)
{
	spinlock_acquire(&curproc->p_spinlock);

	/**
     * Error handling:
     * File descriptors cannot be negative integer numbers
     * or integer greater than the value specified in OPEN_MAX constant.
     * Moreover, there's a function which checks whether the
     * old_fd is a actually existing inside the process filetable or not.
     */
	if (!is_valid_fd(old_fd) || !is_valid_fd(new_fd))
	{
		spinlock_release(&curproc->p_spinlock);
		*err = EBADF;
		return -1;
	}

	/**
     * Old file descriptor equal to the new one.
     * There's no operation to be done.
     */
	if (old_fd == new_fd)
	{
		spinlock_release(&curproc->p_spinlock);
		return 0;
	}

	/**
	 * Allocation of a new openfile.
	 * 
	 * This will host the "new file descriptor" openfile.
	 */ 
	curproc->fileTable[new_fd] = (struct openfile *)kmalloc(sizeof(struct openfile));
	if (curproc->fileTable[new_fd] == NULL)
	{
		spinlock_release(&curproc->p_spinlock);
		*err = ENOMEM;
		return -1;
	}

	/**
	 * Let's now copy all the fields of the fileTable data structure
	 * from the old_fd entry to the new_fd entry.
	 */ 
	curproc->fileTable[new_fd]->vn = curproc->fileTable[old_fd]->vn;
	curproc->fileTable[new_fd]->mode = curproc->fileTable[old_fd]->mode;
	curproc->fileTable[new_fd]->offset = curproc->fileTable[old_fd]->offset;
	curproc->fileTable[new_fd]->accmode = curproc->fileTable[old_fd]->accmode;
	curproc->fileTable[new_fd]->file_lock = curproc->fileTable[old_fd]->file_lock;
	curproc->fileTable[new_fd]->ref_count = curproc->fileTable[old_fd]->ref_count;
	
	/* If ref_count is greater than zero, this means that this entry of the fileTable is still referenced. */
	curproc->fileTable[old_fd]->ref_count++;

	/* Check whether new_fd is previously opened and eventually close it */
	if (curproc->fileTable[old_fd] != NULL && old_fd != STDIN_FILENO &&
		old_fd != STDOUT_FILENO && old_fd != STDERR_FILENO)
	{
		spinlock_release(&curproc->p_spinlock);
		if (sys_close(old_fd, err))
		{
			*err = EINTR;
			return -1;
		}
		spinlock_acquire(&curproc->p_spinlock);
	}

	spinlock_release(&curproc->p_spinlock);

	return 0;
}

/**
 * Implementation of the lseek system call.
 * 
 * Parameters:
 * - fd: the file descriptor
 * - offset: an integer representing the offset
 * - whence: it can be SEEK_CUR, SEEK_SET or SEEK_END
 * - ret_val: pointer to the value returned to syscall.c switch-case call
 * 
 * Return value:
 * - 0, on success
 * - error code, otherwise
 */
off_t sys_lseek(int fd, off_t offset, int whence, int *ret_val)
{
	off_t actual_offset = 0;
	off_t dis;
	struct openfile *of;
	struct stat stat;

	spinlock_acquire(&curproc->p_spinlock);
	/* Checks whether the file descriptor is valid */
	if (fd < 0 || fd > OPEN_MAX || !is_valid_fd(fd))
	{
		*ret_val = EBADF;
		spinlock_release(&curproc->p_spinlock);
		return -1;
	}

	/* Checks whether the whence parameter is valid */
	if (whence != SEEK_CUR && whence != SEEK_SET && whence != SEEK_END)
	{
		*ret_val = EINVAL;
		spinlock_release(&curproc->p_spinlock);
		return -1;
	}

	/* If the offset is zero, we can exit */
	if (offset == 0)
	{
		*ret_val = 0;
		spinlock_release(&curproc->p_spinlock);
		return 0;
	}

	of = curproc->fileTable[fd];

	/**
     * SEEK_SET
     * The offset will simply be the one
     * passed as parameter.
     */
	if (whence == SEEK_SET)
	{
		actual_offset = offset;
	}
	/**
     * SEEK_CUR
     * We need to compute the displacement
     * from the current position adding the
     * offset passed as parameter.
     */
	else if (whence == SEEK_CUR)
	{
		actual_offset = of->offset + offset;
	}
	/**
     * SEEK_END
     * In this case, we need to retrieve the
     * information about the length of the
     * file using a macro defined in vnode.h
     * called VOP_STAT.
     * Then, we will compute the actual_offset following this schema:
     * 
     * <------------------file_length------------------->
     * |start------{current_position}---x<-(offset)->end|
     * <------------------->
     * <--------file_length+offset------------>
     */
	else if (whence == SEEK_END)
	{
		VOP_STAT(of->vn, &stat);
		dis = stat.st_size;
		actual_offset = dis + offset;
	}

	of->offset = actual_offset;

	spinlock_release(&curproc->p_spinlock);
	return actual_offset;
}
