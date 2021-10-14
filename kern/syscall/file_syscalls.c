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


struct fileTable systemFileTable[SYSTEM_OPEN_MAX];

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
sys_read(int fd, userptr_t buf_ptr, size_t size)
{
  int i;
  char *p = (char *)buf_ptr;

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
}

int sys_open(char *filename, int flag, int retfd) {
/*1) opens a file: create an openfile item
*2) obtain vnode from vfs_open()
*3) initialize offset in openfile
return the file descriptor of the openfile item
*/
#if OPT_FILE
	struct proc *proc = curproc; 
	struct openfile *openfile_item; /*create an openfile item*/
	int err; /*return value of vfs_open*/
	int openflags;
	mode_t mode; /*boh*/
	int fd; /*file descriptor --> return value*/
	/*vfs_open prototype: int vfs_open(char *path, int openflags, mode_t mode, struct vnode **ret)*/
	err = vfs_open(filename, openflags, mode, &openfile_item->vn); /*obtain vnode from vfs_open()*/
	openfile_item->offset = 0; /*initialize offset in openfile*/
	
	/*we have to place openfile in systemFileTable*/
	
#else
	return -1; /*non ritorno 0 perchè la sys_open deve ritornare il fd e 0 sarebbe stdin*/
#endif
	
}

int sys_close(int fd) {
#if OPT_FILE

#else
	return -1;
#endif
}

