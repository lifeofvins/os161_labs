/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys_read and sys_write.
 * just works (partially) on stdin/stdout
 */

#include <types.h>
#include <kern/unistd.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>

/*per lab05*/
#include <proc.h>
#include <filetable.h>

/*max num of system wide open file*/
#define SYSTEM_OPEN_MAX 10*MAX_FILES


struct fileTable systemFileTable;

/*
 * simple file system calls for write/read
 */
int
sys_write(int fd, userptr_t buf_ptr, size_t size)
{
  int i;
  char *p = (char *)buf_ptr;

  if (fd!=STDOUT_FILENO && fd!=STDERR_FILENO) {
    kprintf("sys_write supported only to stdout\n");
    return -1;
  }

  for (i=0; i<(int)size; i++) {
    putch(p[i]);
  }

  return (int)size;
}

int
sys_read(int fd, userptr_t buf_ptr, size_t size, int *retval)
{
        char *p = (char *)buf_ptr;
        int i;
#if OPT_FILE
	/*use fd to locate the openfile item from fileTable*/
	int result;

	struct proc *proc = curproc;
	KASSERT(proc != NULL);
	struct openfile *file;
	struct uio userio;
	file = get_file_at_index(proc->perProcessFileTable, fd);
	KASSERT(file != NULL);
	
	/*access offset from openfile (the file is locked)*/
	lock_acquire(file->lock);
	result = VOP_READ(file->vn, userio);
	file->offset = userio.uio_offset;
	KASSERT(result > 0);
	
	/*set *retval to the amount read*/
	for (i = 0; i < (int)size; i++) {
		p[i] = getch();
		if (p[i] < 0)
	 	  return i;
	 }
	*retval = i;
	return 0;
#else

  if (fd!=STDIN_FILENO) {
    kprintf("sys_read supported only to stdin\n");
    return -1;
  }

  for (i=0; i<(int)size; i++) {
    p[i] = getch();
    if (p[i] < 0) 
      return i;
  }

  return (int)size;
  
#endif
}

int sys_open(char *filename, int flag, int retfd) {
/*1) opens a file: create an openfile item
*2) obtain vnode from vfs_open()
*3) initialize offset in openfile
return the file descriptor of the openfile item
*/
#if OPT_FILE
	KASSERT(filename != NULL);
	struct proc *proc = curproc; 
	struct openfile *openfile_item; /*create an openfile item*/
	int err; /*return value of vfs_open*/
	int result; /*result of filetable functions*/
	int openflags;
	mode_t mode; /*boh*/
	int fd; /*file descriptor --> index in the file table and return value*/
	
	/*vfs_open prototype: int vfs_open(char *path, int openflags, mode_t mode, struct vnode **ret)*/
	err = vfs_open(filename, openflags, mode, &openfile_item->vn); /*obtain vnode from vfs_open()*/
	KASSERT(!err);
	openfile_item->offset = 0; /*initialize offset in openfile*/
	
	/*add file to the per process file table*/
	fd = add_file(proc->perProcessFileTable, openfile_item, 0, NULL);
	KASSERT(index > 0); /*returns -1 on error*/
	
	/*add file to the system file table*/
	result = set_file_at_index(systemFileTable, fd, openfile_item);
	KASSERT(result > 0);
	retfd = fd;
	return retfd;
	
	
#else
	return -1; /*non ritorno 0 perchè la sys_open deve ritornare il fd e 0 sarebbe stdin*/
#endif /*OPT_FILE*/
	
}

int sys_close(int fd) {
#if OPT_FILE

	int result;
	/*use fd to locate the openfile item from fileTable*/
	/*remove the openfile item from both the per process fileTable and the system fileTable*/
	KASSERT(fd > 0);
	struct proc *proc = curproc;
	KASSERT(proc != NULL);
	struct openfile *file = NULL;
	file = get_file_at_index(systemFileTable, fd);
	KASSERT(file != NULL);
	result = remove_from_fileTable(systemFileTable, fd);
	KASSERT(result > 0);
	file = get_file_at_index(proc->perProcessFileTable, fd);
	KASSERT(file != NULL);
	result = remove_from_fileTable(proc->perProcessFileTable, fd);
	KASSERT(result > 0);
	kfree(file);
	
	return 0;
	
#else
	return -1;
#endif
}

