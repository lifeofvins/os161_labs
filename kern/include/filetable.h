/*used for lab05 os161*/
#ifndef _FILETABLE_H_
#define _FILETABLE_H_



#include <lib.h>
#include <array.h>
#include <synch.h>
#include <types.h>
#include <vnode.h>

#define MAX_FILES 50

/*definisco la struct openfile*/
#if OPT_FILE
struct openfile {
	struct vnode *vn; /*pointer to vnode*/
	mode_t mode; /*read-only, write-only, read-write*/
	unsigned int offset; /*ad ogni openfile corrisponderà un offset, cioè dove stanno leggendo e scrivendo dentro al file: l'offset avanza man mano che si legge o scrive nel file*/
	struct lock *lock;
	unsigned int ref_count; /*una openfile potrebbe essere condivisa*/
	
};


struct fileTable {
	struct openfile *handles[MAX_FILES+1]; /*static array*/
	unsigned int last; /*index of the last allocated openfile item*/
	struct lock *lock; /*lock for this array*/
} fileTable;


/*functions to deal with filetable*/

/*creates new file table. returns filetable or NULL on error*/
fileTable *create_fileTable(void);

/*attach file table to STDIN, STDOUT, and STDERR: returns 0 on success, -1 on error*/
int init_fileTable(fileTable *ft);

/*adds file to fileTable: returns the index of where the file was added or -1 on error*/
int add_file(fileTable *ft, openfile *file, int init, int *err);

/*removes file at index: returns 1 if removed from tail, 0 from elsewhere, or -1 on error*/
int remove_from_fileTable(fileTable *ft, unsigned int fd);


/* Returns file at index, or NULL on error. */
struct openfile * get_file_at_index (filetable *ft, unsigned int fd);


/*sets file at index to file: returns 0 on success, -1 on failure*/
int set_file_at_index(fileTable *ft, unsigned int fd, openfile *file);

/*copies fileTable: returns 1 or -1 on error*/
int copy_fileTable(fileTable *copyfrom, fileTable *copyto);

/*removes and frees memory for fileTable: returns0 on success, -1 on error.*/
int destroy_fileTable(fileTable *ft);

#endif /*OPT_FILE*/


#endif /*_FILETABLE_H_*/

