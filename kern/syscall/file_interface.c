
#include <types.h>
#include <syscall.h>
#include <lib.h>
#include <uio.h>
#include <copyinout.h>
#include <fdtable.h>
#include <filemacros.h>

int sys_open(const_userptr_t path, int flags, mode_t mode, int* retval)
{
    char *temp;
    int result;
    size_t actual;
    // Make a heap allocation to avoid it going on the stack
    temp = kmalloc( MAXFILEPATHLENGTH * sizeof(char));
    size_t len = MAXFILEPATHLENGTH;
    // copy name from kernel to user space
    result = copyinstr(path, temp, len , &actual);
    if ( result )
    {
        // error handling
        *retval = result;
        kfree(temp);
        return -1;
    }

    // if we are here then we have successfully copied the filename into kernel space
    result = do_sys_open(path, flags, mode, retval);
    
    kfree(temp);
    if( *retval >= 0)
        return 0;

    *retval = result;
    return -1;
}

int sys_write(int fd, const_userptr_t buf, size_t nbytes, int *retval)
{
    (void)fd;
    (void)buf;
    (void) nbytes;
    (void) retval;

    int result;
    result = do_sys_write(fd, buf, nbytes, retval);
    if ( result )
    {
        *retval = result;
        return -1;
    }

    return 0;
}

int sys_read(int fd, userptr_t buf, size_t nbytes, int *retval)
{
    int result;

    result = do_sys_read(fd, buf, nbytes, retval);
    if ( result )
    {
        *retval = result;
        return -1;
    }
    return 0;
}
