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

#if OPT_FILE

#include <copyinout.h>
#include <vnode.h>
#include <vfs.h>
#include <limits.h>
#include <uio.h>
#include <proc.h>

/*max num of system wide open file*/
#define SYSTEM_OPEN_MAX 10*OPEN_MAX


#define USE_KERNEL_BUFFER 0 //cabodi


struct openfile {
	struct vnode *vn; /*pointer to vnode*/
	mode_t mode; /*read-only, write-only, read-write*/
	off_t offset; /*ad ogni openfile corrisponderà un offset, cioè dove stanno leggendo e scrivendo dentro al file: l'offset avanza man mano che si legge o scrive nel file*/
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
	
	if (fd < 0 || fd > OPEN_MAX) return -1;
	of = curproc->fileTable[fd];
	KASSERT(of != NULL);
	vn = of->vn;
	KASSERT(vn != NULL);
	
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
	
	if (fd < 0 || fd > OPEN_MAX) return -1;
	of = curproc->fileTable[fd];
	KASSERT(of != NULL);
	vn = of->vn;
	KASSERT(vn != NULL);
	
	kbuf = kmalloc(size);
	/*siccome devo fare la write metto prima i dati nel buffer e poi scrivo*/
	copyin(buf_ptr, kbuf, size);
	uio_kinit(&iov, &ku, kbuf, size, of->offset, UIO_WRITE);
	result = VOP_WRITE(vn, &ku);
	if(result) return result;
	kfree(kbuf);
	of->offset = ku.uio_offset;
	nwrite = size - ku.uio_resid;
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
	int result; /*result of filetable functions*/

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
			of->ref_count = 1;
			break;
		}
	}
	if (of == NULL) {
		/*non ho trovato posto nella system open file table*/
		*errp = ENFILE;
	}
	else {
		for (fd = STDERR_FILENO+1; fd < OPEN_MAX; fd++) {
			if (curproc->fileTable[fd] == NULL) {
				curproc->fileTable[fd] = of;
				return fd;
			}
		}
		/*no free slot in process open file table*/
		*errp = EMFILE;
	}
	vfs_close(v);
	return -1;
}
int sys_close(int fd) {
	
	struct openfile *of = NULL;
	struct vnode *vn;
	
	if (fd < 0 || fd > OPEN_MAX) return -1;
	of = curproc->fileTable[fd];
	if (of == NULL) return -1;
	curproc->fileTable[fd] = NULL;
	
	if(--of->ref_count > 0) return 0; /*just decrement ref_count*/
	vn = of->vn;
	of->vn = NULL;
	if (vn == NULL) return -1;
	
	vfs_close(vn);
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

  if (fd!=STDOUT_FILENO && fd!=STDERR_FILENO) {
#if OPT_FILE
    return file_write(fd, buf_ptr, size);
#else
    kprintf("sys_write supported only to stdout\n");
    return -1;
#endif
  }

  for (i=0; i<(int)size; i++) {
    putch(p[i]);
  }

  return (int)size;
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


