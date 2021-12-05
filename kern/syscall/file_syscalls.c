/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys_read and sys_write.
 * just works (partially) on stdin/stdout
 */


#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <clock.h>
#include <syscall.h>
#include <current.h>
#include <lib.h>
#include "opt-file.h"

#if OPT_FILE

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
#define SYSTEM_OPEN_MAX 10*OPEN_MAX


#define USE_KERNEL_BUFFER 1 //cabodi


struct openfile {
	struct vnode *vn; /*pointer to vnode*/
	mode_t mode; /*read-only, write-only, read-write*/
	off_t offset; /*ad ogni openfile corrisponderà un offset, cioè dove stanno leggendo e scrivendo dentro al file: l'offset avanza man mano che si legge o scrive nel file*/
	int accmode; 
	struct lock *file_lock;
	unsigned int ref_count; /*una openfile potrebbe essere condivisa*/
}; 

struct openfile systemFileTable[SYSTEM_OPEN_MAX];

void openfileIncrRefCount(struct openfile *of) {
	if (of != NULL) 
		of->ref_count++;
}

#if USE_KERNEL_BUFFER
/*per effettuare una read si deve predisporre opportunamente un puntatore a struct uio ku
(che descrive il tipo di i/o da effettuare) e il puntatore al FCB*/
static int file_read(int fd, userptr_t buf_ptr, size_t size) {
	struct iovec iov;
	struct uio ku;
	int result, nread;
	struct vnode *vn;
	struct openfile *of;
	void *kbuf; /*kernel buffer*/
	
	if (fd < 0 || fd > OPEN_MAX) return EBADF;
	of = curproc->fileTable[fd];
	if (of == NULL) return EBADF;
	vn = of->vn;
	if (vn == NULL) return EINVAL;
	
	/*allocation of kernel buffer*/
	kbuf = kmalloc(size);
	/*predispongo la struttura dati per effettuare la lettura e 
	successivamente faccio la lettura usando come parametri
	soltanto il vnode e il puntatore a ku*/
	uio_kinit(&iov, &ku, kbuf, size, of->offset, UIO_READ); /*sets up a uio data structure*/
	result = VOP_READ(vn, &ku);
	if(result) return result;
	/*i dati vanno a finire in memoria kernel*/
	of->offset = ku.uio_offset;
	nread = size - ku.uio_resid;
	copyout(kbuf, buf_ptr, nread); /*copio nel buffer user nread bytes memorizzati in ku (memoria kernel)*/
	kfree(kbuf);
	return nread;
}

static int file_write(int fd, userptr_t buf_ptr, size_t size) {
	struct iovec iov;
	struct uio ku;
	int result, nwrite;
	struct vnode *vn;
	struct openfile *of;
	void *kbuf;

	struct proc *cur = curproc;
	KASSERT(cur != NULL);
	
	if (fd < 0 || fd > OPEN_MAX) return EBADF;
	of = curproc->fileTable[fd];
	KASSERT(of != NULL);
	if(of==NULL){
		return EBADF;
	}
	vn = of->vn;
	if(vn==NULL)
		return EINVAL;
	
	lock_acquire(of->file_lock);
	if(of->accmode == O_RDONLY){
		lock_release(of->file_lock);
		return EBADF;
	}
	kbuf = kmalloc(size);
	/*siccome devo fare la write metto prima i dati nel buffer e poi scrivo*/
	copyin(buf_ptr, kbuf, size);
	uio_kinit(&iov, &ku, kbuf, size, of->offset, UIO_WRITE);
	if(ku.uio_segflg != UIO_USERSPACE){
		lock_release(of->file_lock);
		return EINVAL; /* ? */	
	}
	result = VOP_WRITE(vn, &ku);
	if(result) {
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
static int file_read(int fd, userptr_t buf_ptr, size_t size) {
	struct iovec iov;
	struct uio u; /*user*/
	struct vnode *vn;
	struct openfile *of;
	int result;
	
	if (fd < 0 || fd > OPEN_MAX) return -1;
	of = curproc->fileTable[fd];
	KASSERT(of != NULL);
	vn = of->vn;
	KASSERT(vn != NULL);
  
  
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
	if(result) return result;
	
	of->offset = u.uio_offset;
	return (size - u.uio_resid);
}

static int file_write(int fd, userptr_t buf_ptr, size_t size) {
	struct iovec iov;
	struct uio u;
	int result;
	struct vnode *vn;
	struct openfile *of;
	
	if (fd < 0 || fd > OPEN_MAX) return -1;
	of = curproc->fileTable[fd];
	KASSERT(of != NULL);
	vn = of->vn;
	KASSERT(vn != NULL);
	
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
	if(result) return result;
	
	of->offset = u.uio_offset;
	
	return (size - u.uio_resid);
}

#endif /*use kernel buffer*/
	

/*file system calls for open/close*/

int sys_open(userptr_t path, int openflags, mode_t mode, int *errp) {
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
	int accmode; /*access mode*/
	int result; /*result of filetable functions*/

	struct proc *cur = curproc;
	KASSERT(cur != NULL);
		/*path pointer check*/
	if(path == NULL){
		*errp = EINVAL;
		return -1;
	}
	
	/*flag check*/
	accmode = openflags & O_ACCMODE;
	if(accmode != O_RDONLY && accmode != O_WRONLY && accmode != O_RDWR){
		*errp = EINVAL;
		return -1;
	}

	/*copy a string from user space to kernel space*/
	result = copyinstr(path, fname, sizeof(fname), NULL);
	
	if(result){
		return  result;
	}

	result = vfs_open((char *)path, openflags, mode, &v); /*obtain vnode from vfs_open()*/
	if(result) {
	  *errp = ENOENT;
	  return -1;
	}
	
	/*search in system open file table*/
	for(i = 0; i < SYSTEM_OPEN_MAX; i++) {
		/*search for free pos in which place the openfile struct*/
		if (systemFileTable[i].vn == NULL) {
			of = &systemFileTable[i];
			of->vn = v;
			of->offset = 0; /*initialize offset. TODO: handle offset with append*/
			of->accmode = accmode;
			of->file_lock = lock_create(fname);
			of->ref_count = 1;
			break;
		}
	}
	if(of->file_lock == NULL){
		vfs_close(v);
		*errp = ENOMEM;	
		return -1;
	}
	if (of == NULL) {
		/*non ho trovato posto nella system open file table*/
		*errp = ENFILE;
		lock_destroy(of->file_lock);
	}
	else {
		if(openflags & O_APPEND){
			result = VOP_STAT(of->vn, &st);
			if(result){
				vfs_close(v);
				*errp = EINVAL;
				return -1;
			}
			of->offset = st.st_size;
		}
		for (fd = STDERR_FILENO+1; fd < OPEN_MAX; fd++) {
			if (curproc->fileTable[fd] == NULL) {
				curproc->fileTable[fd] = of;
				return fd;
			}
		}
		/*no free slot in process open file table*/
		*errp = EMFILE;
	}
	/*if I'm here, something went wrong*/
	vfs_close(v);
	return -1;
}
int sys_close(int fd) {
	struct openfile *of = NULL;
	struct vnode *vn;
	
	struct proc *cur = curproc;
	KASSERT(cur != NULL);

	/*Incorrect file descriptor*/
	if(fd<0||fd>OPEN_MAX){
		return EBADF;
	}

	of = curproc->fileTable[fd];

	if(of==NULL){
		return EBADF;
	}

	curproc->fileTable[fd] = NULL;
	
	vn = of->vn;
	of->vn = NULL;
	if(vn==NULL)
		return EINVAL; /* ? */
	
	/*if(--of->ref_count>0)
		return 0;*/

	lock_acquire(of->file_lock);
	/*if it is the last close of this file, free it up*/
	if(of->ref_count == 1){
		vfs_close(vn);
		lock_release(of->file_lock);
		lock_destroy(of->file_lock);
	}	
	else{
		KASSERT(of->ref_count > 1);	
		of->ref_count--;
		lock_release(of->file_lock);
	}

	return 0;
}

#endif /*OPT_FILE riga 15*/




/*
 * simple file system calls for write/read
 */
int
sys_write(int fd, userptr_t buf_ptr, size_t size)
{
	int i;
	char *p = (char *)buf_ptr;

	struct proc *cur = curproc;
	KASSERT(cur != NULL);
	
	if (fd == STDIN_FILENO) {
		/*we cannot write on stdin*/
		return -1;
	}
	if(fd == STDOUT_FILENO || fd == STDERR_FILENO)
	{
		for(i = 0;i < (int)size;i++)
		{
			putch(p[i]);
		}
	}
	else {
#if 	OPT_FILE
	return file_write(fd, buf_ptr, size);
#else
	return -1;
#endif
	}

return 1;	
}
int
sys_read(int fd, userptr_t buf_ptr, size_t size)
{
        char *p = (char *)buf_ptr;
        int i;

  if (fd!=STDIN_FILENO) {
#if OPT_FILE
    return file_read(fd, buf_ptr, size);
#else
    kprintf("sys_read supported only to stdin\n");
    return -1;
#endif
  }

  for (i=0; i<(int)size; i++) {
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
 * - ret_val: integer pointer set to 0 if it's all
 *            gone ok, -1 otherwise.
 * 
 * Example of usage:
 *      int logfd = open("logfile", O_WRONLY);
 *      dup2(logfd, STDOUT_FILENO); 
 *      close(logfd);
 *      printf("Hello, OS161.\n");
 * 
 * 
 * Return value:
 * 0: operation completed successfully
 * error code if there's been an error. 
 */

int
sys_dup2(int old_fd, int new_fd, int *ret_val)
{
    struct openfile of;
    /**
     * Error handling:
     * File descriptors cannot be negative integer numbers
     * or integer greater than the value specified in OPEN_MAX constant.
     * Moreover, there's a function which checks whether the
     * old_fd is a actually existing inside the process filetable or not.
     */
    if (!is_valid_fd(old_fd) || !is_valid_fd(new_fd))
    {
        *ret_val = -1;
        return EBADF;
    }

	//TODO return EMFILE if the process file table was full, or a process-specific limit on open files was reached

    /**
     * Old file descriptor equal to the new one.
     * There's no operation to be done, but
     * ret_val's content is set to 0xFF so that
     * it's possible to know that old_fd and new_fd
     * are the same.
     * Actually useless.
     * Probably it will be removed.
     */
    if (old_fd == new_fd)
    {
        *ret_val = 0xFF;
		//*ret_val = new_fd;

		/*from linux man page: if oldf == newf then dup2 returns newf*/
        return 0;
    }

    /* Check whether new_fd is previously opened and eventually close it */
    if (systemFileTable[new_fd].vn != NULL)
    {
        sys_close(new_fd);
    }

    // /* Open the new_fd descriptor */
    // if (sys_open(new_fd) == -1)
    // {
    //     *ret_val = -1;
    //     return EBADF;
    // }

    /* Let the entry related to new_fd point to the old_fd's one */
    of = systemFileTable[old_fd];
    systemFileTable[new_fd].vn = of.vn;
    systemFileTable[new_fd].mode = of.mode;
    systemFileTable[new_fd].offset = of.offset;
    systemFileTable[new_fd].ref_count = of.ref_count;

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
int
sys_lseek(int fd, off_t offset, int whence, int *ret_val)
{

	//TODO handle 64-bit parameter and 64-bit return value
    off_t actual_offset = 0;
    off_t dis;
    struct openfile *of;
    struct stat stat;

    spinlock_acquire(&curproc->p_spinlock);
    /* Checks whether the file descriptor is valid */
    if (!is_valid_fd(fd))
    {
        *ret_val = -1;
        spinlock_release(&curproc->p_spinlock);
        return EBADF;
    }

    /* Checks whether the whence parameter is valid */
    if (whence != SEEK_CUR && whence != SEEK_SET && whence != SEEK_END)
    {
        *ret_val = -1;
        spinlock_release(&curproc->p_spinlock);
        return EINVAL;
    }

    /* If the offset is zero, we can exit */
    if(offset == 0)
    {
        *ret_val = 0;
        spinlock_release(&curproc->p_spinlock);
        return 0;
    }

    of = &systemFileTable[fd];
    
    /**
     * SEEK_SET
     * The offset will simply be the one
     * passed as parameter.
     */
    if(whence == SEEK_SET)
    {
        actual_offset = offset;
    }
    /**
     * SEEK_CUR
     * We need to compute the displacement
     * from the current position adding the
     * offset passed as parameter.
     */
    else if(whence == SEEK_CUR)
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
    else if(whence == SEEK_END)
    {
        VOP_STAT(of->vn, &stat);
        dis = stat.st_size;
        actual_offset = dis + offset;
    }

    of->offset = actual_offset;
    
    spinlock_release(&curproc->p_spinlock);
    return 0;
}

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
int
is_valid_fd(int fd)
{
    if (fd < 0 || fd > OPEN_MAX)
        return 0;
    return !(curproc->fileTable[fd]->vn == NULL);
}