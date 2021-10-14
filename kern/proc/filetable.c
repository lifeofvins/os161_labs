/*for lab05 os161, taken from github*/
#if OPT_FILE


#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/seek.h>
#include <kern/unistd.h>
#include <filetable.h>
#include <lib.h>
#include <vfs.h>
#include <vnode.h>
#include <synch.h>

/*qui definisco le funzioni della fileTable*/

struct fileTable *create_fileTable(void) {
	int i; /*index*/
	struct fileTable *ft = NULL;
	
	/*allocation*/
	ft = (fileTable *)kmalloc(sizeof(fileTable));
	KASSERT(ft != NULL);
	
	/*initialize handles (MAX_FILES is defined as constant in filetable.h)*/
	for (i = 0; i < MAX_FILES; i++)	
		ft->handles[i] = NULL;
		
	ft->last = 0; /*at the beginning the fileTable is "empty"*/
	ft->lock = lock_create("fileTable");
	
	return ft;
}

int init_fileTable(fileTable *ft) {
	struct vnode *v = NULL;
	openfile *fd_IN = NULL;
	openfile *fd_OUT = NULL;
	openfile *fd_ERR = NULL;
	
	/*non so a che servono*/
	char *con1 = NULL;
	char *con2 = NULL;
	char *con3 = NULL;
	int err;
	
	KASSERT(ft != NULL);
	
	/*STDIN*/
	fd_IN = (openfile *)kmalloc(sizeof(openfile)); /*allocation*/
	KASSERT(fd_IN != NULL);
	
	con1 = kstrdup("con:"); 
	
	err = vfs_open(con1, O_RDONLY, 0, &v);
	if (err) {
		vfs_close(v);
		destroy_fileTable(ft);
		return -1;
	}
	fd_IN->vn = v;
	fd_IN->offset = 0;
	fd_IN->mode = O_RDONLY;
	fd_IN->refs = 0;
	fd_IN->lock = lock_create("stdin");
	add_file(ft, fd_IN, 1, NULL);
	
	/*STDOUT*/
	fd_OUT = (openfile *)kmalloc(sizeof(openfile));
	KASSERT(fd_OUT != NULL);
	
	con2 = kstrdup("con:");
	err = vfs_open(con2, O_WRONLY, 0, &v);
	if (err) {
		vfs_close(v);
		destroy_fileTable(ft);
		return -1;
	}
	
	fd_OUT->vn = v;
	fd_OUT->offset = 0;
	fd_OUT->mode = O_WRONLY;
	fd_OUT->lock = lock_create("stdout");
	add_file(ft, fd_OUT, 1, NULL);
	
	/*STDERR*/
	fd_ERR = (openfile *)kmalloc(sizeof(openfile));
	KASSERT(fd_ERR != NULL);
	
	con3 = kstrdup("con:");
	
	err = vfs_open(con3, O_WRONLY, 0, &v);
	if(err) {
		vfs_close(v);
		destroy_fileTable(ft);
		return -1;
	}
	
	fd_ERR->vn = v;
	fd_ERR->offset = 0;
	fd_ERR->mode = O_WRONLY;
	fd_ERR->lock = lock_create("stderr");
	add_file(ft, fd_ERR, 1, NULL);
	
	
	/*free*/
	kfree(con1);
	kfree(con2);
	kfree(con3);
	
	return 0;	
}

int add_file(fileTable *ft, openfile *file, int init, int *err) {
	
	unsigned int pos; /*position in the handles array*/
	KASSERT(ft != NULL);
	KASSERT(file != NULL);
	
	/*non so a che serve sto if*/
	if (init == 0) 
		init_fileTable(ft);
		
	/*mutual exclusion*/
	lock_acquire(ft->lock);
	
	pos = 0;
	while (ft->handles[ft->last] != NULL) {
		ft->last++;
		if (ft->last == MAX_FILES)
			ft->last = 0; /*circular strategy*/
		pos++;
		if (pos >= MAX_FILES) 
			*err = EMFILE;
	}
	
	ft->handles[ft->last] = file;
	ft->handles[ft->last]->ref_count++;
	
	lock_release(ft->lock);
	
	return (ft->last);
}

int remove_from_fileTable(fileTable *ft, unsigned int fd) {
	
	struct openfile *file = NULL;
	KASSERT(ft != NULL);
	KASSERT(fd < MAX_FILES);
	
	lock_acquire(ft->lock);
	
	file = ft->handles[fd];
	if (file != NULL) {
		file->ref_count--;
		if (file->ref_count == 0) {
			vfs_close(file->vn);
			kfree(file);
		}
		ft->handles[fd] = NULL;
		if (fd < ft->last)
			ft->last = fd;
	}
	else {
		lock_release(ft->lock);
		return EBADF; /*bad file descriptor*/
	}
	lock_release(ft->lock);
	
	return 0;
}

struct openfile * get_file_at_index (filetable *ft, unsigned int fd) {

	openfile *file = NULL;
	KASSERT(ft != NULL);
	KASSERT(fd < MAX_FILES);
	
	if(ft->handles[0] == NULL)
		init_fileTable(ft);
	lock_acquire(ft->lock);
	file = ft->handles[fd];
	lock_release(ft->lock);
	
	return file;
}

int set_file_at_index(fileTable *ft, unsigned int fd, openfile *file) {
	
	KASSERT(ft != NULL);
	KASSERT(fd < MAX_FILES);
	
	if (ft->handles[0] == NULL)
		init_fileTable(ft);
	lock_acquire(ft->lock);
	ft->handles[fd] = file;
	lock_release(ft->lock);
	
	return 0;
}

int copy_fileTable(fileTable *copyfrom, fileTable *copyto) {
	
	int i;
	KASSERT(copyfrom != NULL);
	KASSERT(copyto != NULL);
	
	lock_acquire(copyfrom->lock);
	
	for (i = 0; i < MAX_FILES; i++) {
		if(copyfrom->handles[i] != NULL) {
			add_file(copyto, copyfrom->handles[i], 1, NULL);
			copyto->handles[i]->ref_count++;
		}
	}
	copyto->last = copyfrom->last;
	
	lock_release(copyfrom->lock);

}

int destroy_fileTable(fileTable *ft) {
	
	int i;
	
	KASSERT(ft != NULL);
	
	for(i = 0; i < MAX_FILES; i++)
		remove_from_fileTable(ft, i);
	
	lock_destroy(ft->lock);
	kfree(ft);
	
	return 0;

}

#endif /*OPT_FILE*/
