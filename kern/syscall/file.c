/**
 * @file:   file.c
 * @brief:  implementation of io syscalls, open, write, close, lseek, dup2
 * @author: bladechen(chenshenglong1990@gmail.com)
 *
 * 2016-12-10
 */
#include <types.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>
#include <kern/errno.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>

#include <debug_print.h>
#include "fdtable.h"

#define MAX_FILENAME_LENGTH 128

int syscall_open(const_userptr_t filename, int flags, mode_t mode, int* fd_num)
{
    int result = 0;
    void* tmp_filename = kmalloc(MAX_FILENAME_LENGTH);
    size_t tmp_filename_len = 0;

    if (tmp_filename == NULL)
    {
        DEBUG_PRINT("no enough kernel mem\n");
        * fd_num = ENOMEM;
        return -1;
    }

    result = copyinstr(filename, tmp_filename, MAX_FILENAME_LENGTH - 1, & tmp_filename_len);
    if (result != 0)
    {
        DEBUG_PRINT("copy open filename to kernel buf error: %d\n", result);
        kfree(tmp_filename);
        *fd_num = result;
        return -1;
    }
    DEBUG_PRINT("copy filename success: %s\n", (char*)tmp_filename);
    result  = do_sys_open(-1, tmp_filename, flags, mode, get_current_proc()->fs_struct);
    kfree(tmp_filename);
    if (result < 0)
    {
        *fd_num = result;
        return -1;
    }
    *fd_num = result;
    return 0;
}

int syscall_close(int fd_num, int* retval)
{
    *retval = do_sys_close(fd_num);
    return (*retval == 0 )? 0 : -1;
}
int syscall_read(int fd, userptr_t buf, size_t buflen, size_t* retval)
{

    DEBUG_PRINT("fd: %d, read\n", fd);
    void*  tmp_kern_buf = kmalloc(buflen + 1);

    if (tmp_kern_buf == NULL)
    {
        DEBUG_PRINT ("no enough kernel mem\n");
        *retval  = ENOMEM;
        /* return -ENOMEM; */
        return -1;
    }

    int result = do_sys_read(fd, tmp_kern_buf, buflen);
    if (result < 0)
    {
        DEBUG_PRINT ("do sys read errorn\n");
        kfree(tmp_kern_buf);
        *retval = -result;
        return -1;
    }
    if (result == 0)
    {
        kfree(tmp_kern_buf);
        *retval = result;
        return 0;
    }

/* int */
/* copyoutstr(const char *src, userptr_t userdest, size_t len, size_t *actual) */

    *retval  = result;
    result = copyout(tmp_kern_buf, buf, result);
    if (result != 0)
    {
        kfree(tmp_kern_buf);
        *retval = result;
        return -1;
    }
    kfree(tmp_kern_buf);
    return 0;
}

int syscall_write(int fd,  const_userptr_t buf, size_t nbytes, size_t* retval)
{

    if (nbytes == 0)
    {
        int result = do_sys_write(fd, NULL, 0);
        *retval = result;
        return (*retval == 0)? 0: -1;

    }
    /* size_t tmp_kern_buflen = nbytes + 1; */
    /* if (nbytes) */
    char*  tmp_kern_buf = kmalloc(nbytes + 1);

    if (tmp_kern_buf == NULL)
    {
        DEBUG_PRINT ("no enough kernel mem\n");
        *retval = ENOMEM;
        return -1;
    }

    int result = copyin(buf, tmp_kern_buf,  nbytes);
    if (result != 0)
    {
        DEBUG_PRINT ("copy write user buf to  kernel buf error: %d\n", result);
        kfree(tmp_kern_buf);
        *retval = result;
        return -1;
    }
    tmp_kern_buf[nbytes] = 0;
    /* DEBUG_PRINT ("copyin[%s], %d\n", (char*) tmp_kern_buf, nbytes); */
    result = do_sys_write(fd, tmp_kern_buf, nbytes);
    if (result < 0)
    {
        kfree(tmp_kern_buf);
        *retval = - result;
        return -1;
    }
    *retval = result;
    kfree(tmp_kern_buf);
    return 0;
}
int syscall_lseek(int fd, off_t pos, int whence, off_t* retval)
{
    *retval = 0;
    off_t r = do_sys_lseek(fd, pos, whence);
    if (r < 0)
    {
        *retval = -r;
        return -1;
    }
    KASSERT(r >= 0);
    *retval = r;
    return 0;

}
int syscall_dup2(int oldfd, int newfd, int* retval)
{
    *retval  = do_sys_dup2(oldfd, newfd);
    return (*retval == 0)? 0: -1;

}
/* int syscall_write(int fd_num, const_userptr_t* buf, size_t length) */
/* { */
/*     int written_length = 0; */
/*     return written_length; */
/* } */
