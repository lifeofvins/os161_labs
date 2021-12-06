/*
 * Author: Vincenzo Sagristano
 * Implementation of system calls for
 * managing the current working directory.
 */

#include <kern/types.h>
#include <types.h>
#include <kern/unistd.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>
#include <kern/errno.h>
#include <kern/limits.h>
#include <vfs.h>
#include <uio.h>
#include <kern/iovec.h>
#include <current.h>
#include <copyinout.h>
#include <proc.h>

int dir_parser(const char *);
void user_uio_kinit(struct uio *, struct iovec *, userptr_t, size_t);

/**
 * getcwd: get the current working directory
 * 
 * Parameters:
 * - buf: pointer to the buffer which will store the cwd
 * - size: size of the buffer
 * - return_value: this contains
 *      -> -1 on failure
 *      -> the amount of data read on success
 * 
 * Return value:
 * - 0: success
 * - error_code: failure
 */
int sys_getcwd(userptr_t buf, size_t size, int *return_value)
{
    KASSERT(buf != 0x00);
    KASSERT(size > 0);

    struct uio uio;
    struct iovec iovec;
    int vfs_return_value;

    /* 
        This could be a useful check for whether the 
        current working director is a NULL pointer
    */

    struct vnode *cwd_vn = curproc->p_cwd;
    if (cwd_vn == 0x00)
    {
        return ENOENT;
    }

    /* Setting the user space without involving the kernel level address space */
    user_uio_kinit(&uio, &iovec, buf, size);

    vfs_return_value = vfs_getcwd(&uio);
    if (vfs_return_value != 0)
    {
        *return_value = -1;
        return vfs_return_value;
    }

    /**
     * return_value contains how many characters have
     * been read. uio.uio_resid contains the 
     * "remaining amt of data to xfer", so
     * size - uio.uio_resid is equal to the
     * amount of read data.
     */
    *return_value = 0;

    return size - uio.uio_resid;
}

/**
 * chdir: change the current working directory
 * 
 * Parameters:
 * - path: the new path to which the cwd has to be set
 * 
 * Return value:
 * - 0, if success
 * - -1, otherwise
 */
int sys_chdir(userptr_t path)
{
    KASSERT(path != 0x00);

    int vfs_return_value;
    char *kpath = NULL;

    /* Check whether the new dir path is valid or not */
    if (dir_parser((const char *)path))
        return -1;

    copyinstr(path, kpath, __PATH_MAX, NULL);

    vfs_return_value = vfs_chdir(kpath);

    if (vfs_return_value)
        return vfs_return_value;

    return 0;
}

/**
 * Directory parser.
 * Checks whether the new_path string
 * represent a valid path from a 
 * syntactical point of view;
 * 
 * Parameter:
 * - dir: string containing the new path
 * 
 * Return value:
 * - 0: success
 * - 1: bad path
 * - 2: memory allocation failed
 */
int dir_parser(const char *dir)
{
    char *l1_dir = (char *)kmalloc(sizeof(dir));
    char *_tmp_free_ptr;
    char prev = '\0';

    if (l1_dir == NULL)
        return -2;

    strcpy(l1_dir, dir);
    _tmp_free_ptr = l1_dir;

    /* First-layer parser */
    for (; *l1_dir != '\0'; l1_dir++)
    {
        if (prev == '\0')
        {
            prev = *l1_dir;
            continue;
        }
        if (*l1_dir == '/' && prev == '/')
            return 1;
        prev = *l1_dir;
    }

    /* Free */
    kfree(_tmp_free_ptr);

    return 0;
}

/**
 * user_uio_kinit
 * 
 * Sets the user space without 
 * involving the kernel level
 * address space.
 */
void user_uio_kinit(struct uio *uio, struct iovec *iovec, userptr_t buf, size_t size)
{
    uio_kinit(iovec, uio, buf, size, 0, UIO_READ);
    uio->uio_segflg = UIO_USERSPACE;
    uio->uio_space = proc_getas();
}