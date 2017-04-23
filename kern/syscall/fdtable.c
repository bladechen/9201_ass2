#include <types.h>
#include <kern/seek.h>
#include <kern/errno.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <elf.h>


#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>

#include <types.h>
#include <copyinout.h>
#include <syscall.h>

#include <vnode.h>
#include <debug_print.h>
#include <proc.h>
#include "membar.h"
#include "fdtable.h"
#include "file.h"


static int get_rightmost_zero_bit(unsigned int in)
{
    return ((~in) & (in + 1));
}
static int __get_bit(int nr, volatile void * addr)
{
    KASSERT(nr >= 0);
     int *m = ((int *) addr) + (nr >> 5);
     int ret = (*m &( 1 << (nr & 31)));
     membar_any_any();
     return ret;
}
/*
 * >=0 success, < 0 indicates the errno
 */

/* static int bit_2_index[8] = {}; */
static int find_next_fd(struct fdtable *fdt)
{
    int fd = 0;
    for (size_t i = 0; i < fdt->max_fds / FD_BITS ; i ++)
    {
        int bit = get_rightmost_zero_bit(fdt->open_fds_bits[i]);
        if (bit != 0)
        {
            for (size_t j = 0; j < FD_BITS; j ++)
            {
                if ((1 << j) == bit)
                {
                    return fd + j;
                }

            }
        }
        fd += FD_BITS;
    }
    return -1;
}
static struct file* __fd_check(struct files_struct* fst, int fd)
{
    KASSERT(spinlock_do_i_hold(&(fst->file_lock)));

    if (is_valid_fd(fst, fd) && fst->fdt->fd_array[fd] != NULL)
    {
        KASSERT(__get_bit(fd, fst->fdt->open_fds_bits)  !=  0);
        return fst->fdt->fd_array[fd];
    }
    else
    {
        return NULL;
    }
}
static void __set_bit(int nr, volatile void * addr)
{
    KASSERT(nr >= 0);
     int *m = ((int *) addr) + (nr >> 5);
     *m |= 1 << (nr & 31);
     membar_any_any();
}
static void __clear_bit(int nr, volatile void * addr)
{
    KASSERT(nr >= 0);
     int *m = ((int *) addr) + (nr >> 5);
     *m &= ~(1 << (nr & 31));
     /* DEBUG_PRINT("nr: %d, index: %d, bit: %d\n", nr, (nr >> 5), 1 << (nr & 31)); */
     membar_any_any();
}
static void __set_open_fd(int fd,  struct fdtable *fdt)
{
    __set_bit(fd, fdt->open_fds_bits);
}
/*
 * currently only support fixed count fd, TODO reallocate fd array if filled up
 */
static int __alloc_fd(struct files_struct* files)
{
    KASSERT(files != NULL);
    int fd;
    spinlock_acquire(&(files->file_lock));
    struct fdtable* fdt = files->fdt;
    fd = find_next_fd(fdt);
    if (fd < 0)
    {
        spinlock_release(&(files->file_lock));
        return -EMFILE;
    }
    __set_open_fd(fd, fdt);
    spinlock_release(&(files->file_lock));
    return fd;
}
static void __put_unused_fd(struct files_struct* files, int fd)
{
    struct fdtable *fdt = files->fdt;
    __clear_bit(fd, fdt->open_fds_bits);
    return;
}


static int __close_fd(struct files_struct* files, int fd)
{
    if (fd < 0)
    {
        return -EBADF;
    }
    struct fdtable *fdt;
    struct file *file;
    spinlock_acquire(&(files->file_lock));
    fdt = files->fdt;
    if (fd >=(int) fdt->max_fds)
    {
        spinlock_release(&(files->file_lock));
        return -EBADF;
    }
    file = __fd_check(files, fd);
    if (file == NULL)
    {
        spinlock_release(&(files->file_lock));
        return -EBADF;
    }
    /* file = fdt->fd_array[fd]; */
    KASSERT(file != NULL);
    fdt->fd_array[fd] = NULL;
    __put_unused_fd(files, fd);
    return close_kern_file(file, &(files->file_lock));
}
static void __fd_install(struct files_struct* fst, int fd, struct file* fp)
{
    KASSERT(fp != NULL);
    KASSERT(fd >= 0);
    KASSERT(fd < (int)fst->fdt->max_fds);
    KASSERT(fst->fdt->fd_array[fd] == NULL);
    KASSERT(__get_bit(fd, fst->fdt->open_fds_bits) != 0);
    fst->fdt->fd_array[fd] = fp;
    return;
}

/*
 * there is intermediate status which is:
 * open_fd_used has been marked 1,
 * but the file pointer has not been installed in fd array
 *
 */
int do_sys_open(int dfd, void* filename, int flags, mode_t mode, struct files_struct* fst)
{
    /* struct files_struct* fst = get_current_proc()->fs_struct; */
    KASSERT(fst != NULL);
    int fd = __alloc_fd(fst);
    if (fd < 0)
    {
        DEBUG_PRINT("allocate fd error, while opening: %s\n", (char*)filename);
        return fd;
    }
    struct file* fp = NULL;
    int ret  = do_flip_open(&fp, dfd, filename, flags, mode);
    if ( ret != 0)
    {
        DEBUG_PRINT("do flip open error, while opening: %s, put back fd: %d\n", (char*)filename, fd);
        __put_unused_fd(fst, fd);
        KASSERT(ret < 0);
        return ret;
    }
    else
    {
        __fd_install(fst, fd, fp);
    }
    /* DEBUG_PRINT("do open %s, %d success\n", (char*)filename, fd); */
    return fd;
}
int do_sys_close(int fd)
{
    return __close_fd(get_current_proc()->fs_struct, fd);
}


static int __dup2(struct files_struct* fst, struct file* f, int newfd)
{
    KASSERT(spinlock_do_i_hold(&(fst->file_lock)));
    struct fdtable *fdt;
    fdt = fst->fdt;
    struct file* tofree =  fdt->fd_array[newfd];
    /*
     * imtermediate status
     */
    if (tofree == NULL && __get_bit(newfd, fdt->open_fds_bits ) != 0)
    {
        spinlock_release(&(fst->file_lock));
        return -EBUSY;
    }
    /* KASSERT(tofree == NULL); */
    /* KASSERT(__get_bit(newfd, fdt->open_fds_bits) == 0); */
    inc_ref_file(f);
    fdt->fd_array[newfd] = f;
    __set_open_fd(newfd, fdt);
    spinlock_release(&(fst->file_lock));
    if (tofree != NULL)
    {
        spinlock_acquire(&(fst->file_lock));
        close_kern_file(tofree, &(fst->file_lock));
    }
    return 0;

}
int do_sys_dup2(int oldfd, int newfd)
{
    if (oldfd == newfd)
    {
        return -EINVAL;
    }
    struct files_struct* fst = get_current_proc()->fs_struct;
    if (is_valid_fd(fst, oldfd) == 0 ||
        is_valid_fd(fst, newfd) == 0)
    {
        return -EBADF;
    }
    spinlock_acquire(&(fst->file_lock));
    struct file* f = __fd_check(fst, oldfd);
    if (f == NULL)
    {
        spinlock_release(&(fst->file_lock));
        return -EBADF;
    }
    /* spinlock of file_lock should be released inside __dup2 */
    return  __dup2(fst, f, newfd);

}

off_t do_sys_lseek(int fd, off_t pos, int whence)
{
    if (whence != SEEK_SET
        && whence != SEEK_CUR
        && whence != SEEK_END)
    {
        return -EINVAL;
    }
    struct files_struct* fst = get_current_proc()->fs_struct;
    if (is_valid_fd(fst,fd) == 0)
    {
        return -EBADF;
    }

    spinlock_acquire(&(fst->file_lock));
    struct file* f = __fd_check(fst, fd);
    if (f == NULL)
    {
        spinlock_release(&(fst->file_lock));
        return -EBADF;
    }

    /* a trick played here
     * I have some operation on this file handler, although some other threads may close this file handler via fd, but this file handler would not be released after the operations finished.
     */
    inc_ref_file(f);

    spinlock_release(&(fst->file_lock));

    off_t ret = kern_file_seek(f, pos, whence);
    spinlock_acquire(&(fst->file_lock));
    if ( ret < 0)
    {
        close_kern_file(f, &(fst->file_lock));
        return ret;
    }

    close_kern_file(f, &(fst->file_lock));
    return ret;

}
int do_sys_read(int fd, char* buf, size_t buf_len)
{

    struct files_struct* fst = get_current_proc()->fs_struct;
    if (is_valid_fd(fst, fd) == 0)
    {
        return -EBADF;
    }

    spinlock_acquire(&(fst->file_lock));
    struct file* f = __fd_check(fst, fd);
    if (f == NULL)
    {
        spinlock_release(&(fst->file_lock));
        return -EBADF;
    }
    inc_ref_file(f);
    spinlock_release(&(fst->file_lock));
    size_t read_len = 0;
    int ret = kern_file_read(f, buf, buf_len, &read_len);
    spinlock_acquire(&(fst->file_lock));
    if ( ret != 0)
    {
        close_kern_file(f, &(fst->file_lock));
        KASSERT(ret < 0);
        return ret;
    }

    close_kern_file(f, &(fst->file_lock));
    return read_len;

}
ssize_t do_sys_write(int fd, const void *buf, size_t buf_len)
{

    struct files_struct* fst = get_current_proc()->fs_struct;
    if (is_valid_fd(fst, fd) == 0)
    {
        return -EBADF;
    }

    spinlock_acquire(&(fst->file_lock));
    struct file* f = __fd_check(fst, fd);
    if (f == NULL)
    {
        spinlock_release(&(fst->file_lock));
        return -EBADF;
    }
    inc_ref_file(f);
    spinlock_release(&(fst->file_lock));
    size_t write_len= 0;
    int ret = kern_file_write(f, buf, buf_len, & write_len);
    spinlock_acquire(&(fst->file_lock));
    if (ret != 0)
    {
        close_kern_file(f, &(fst->file_lock));
        KASSERT(ret < 0);
        return ret;
    }

    close_kern_file(f, &(fst->file_lock));
    return write_len;


}

                                                                                                              int copy_proc_fd_table(struct proc* from, struct proc* to)
{
	KASSERT(from != NULL && to != NULL);
	spinlock_acquire(&(from->fs_struct->file_lock));
	for (int i = 3; i < (int)from->fs_struct->fdt->max_fds; ++ i)
	{
		if (__get_bit(i, from->fs_struct->fdt->open_fds_bits) )
        {
            KASSERT(from->fs_struct->fdt->fd_array[i] != NULL);
            __set_open_fd(i, to->fs_struct->fdt);
            to->fs_struct->fdt->fd_array[i] = from->fs_struct->fdt->fd_array[i];
            inc_ref_file(to->fs_struct->fdt->fd_array[i]);

        }

	}
	spinlock_release(&(from->fs_struct->file_lock));


     return 0;
 }
static void __destroy_fdt(struct fdtable* fdt, struct files_struct* fst)
{
    for (size_t i = 0; i < fdt->max_fds; i ++)
    {
        if (__get_bit(i, fdt->open_fds_bits) )
        {
            KASSERT(fdt->fd_array[i] != NULL);
            spinlock_acquire(&(fst->file_lock));
            close_kern_file(fdt->fd_array[i], &(fst->file_lock));
            fdt->fd_array[i] = NULL;
            __clear_bit(i, fdt->open_fds_bits);
        }

    }
    kfree(fdt->fd_array);
    kfree((void*)fdt->open_fds_bits);
    return;
}
static int __init_fdt(struct fdtable* fdt)
{
    KASSERT(fdt != NULL);
    fdt->max_fds = MAX_FD_COUNT_PER_PROCESS;
    fdt->fd_array = kmalloc(MAX_FD_COUNT_PER_PROCESS * sizeof(struct file*));
    if (fdt->fd_array == NULL)
    {
        return -1;
    }
    DEBUG_PRINT("memset %p\n", fdt->fd_array);
    memset((fdt->fd_array), 0, MAX_FD_COUNT_PER_PROCESS * sizeof(struct file*));
    fdt->open_fds_bits = kmalloc(MAX_FD_COUNT_PER_PROCESS/FD_BITS * (sizeof(unsigned int)));
    if (fdt->open_fds_bits == NULL)
    {
        kfree(fdt->fd_array);
        return -1;
    }
    for (size_t i = 0; i < MAX_FD_COUNT_PER_PROCESS/FD_BITS; i ++)
    {
        fdt->open_fds_bits[i] = 0;
        fdt->fd_array[i] = NULL;
    }
    return 0;
}
int init_stdio(struct files_struct* fst)
{
    /* (void)fst; */
    int ret = do_sys_open(0, NULL, 0, 0, fst);
    if ( ret < 0)
    {
        return ret;
    }

    ret = do_sys_open(1, NULL, 0, 0, fst);
    if ( ret < 0)
    {
        return ret;
    }

    ret = do_sys_open(2, NULL, 0, 0, fst);
    if ( ret < 0)
    {
        return ret;
    }

    return 0;
}

int init_fd_table(struct proc* cur)
{

    KASSERT(cur != NULL);
    cur->fs_struct = kmalloc(sizeof(*(cur->fs_struct)));
    if (cur->fs_struct == NULL)
    {
        return ENOMEM;
    }
    spinlock_init(&(cur->fs_struct->file_lock));
    cur->fs_struct->fdt = kmalloc(sizeof((*cur->fs_struct->fdt)));
    if (cur->fs_struct->fdt == NULL)
    {
        spinlock_cleanup(&(cur->fs_struct->file_lock));
        kfree(cur->fs_struct);
        cur->fs_struct = NULL;
        return ENOMEM;
    }
    int ret = __init_fdt(cur->fs_struct->fdt);
    if (ret != 0)
    {
        kfree(cur->fs_struct->fdt);
        spinlock_cleanup(&(cur->fs_struct->file_lock));
        kfree(cur->fs_struct);
        cur->fs_struct = NULL;
        return ENOMEM;
    }
    /* ret = __init_stdio(cur->fs_struct); */
    /* if ( ret != 0) */
    /* { */
    /*     __destroy_fdt(cur->fs_struct->fdt); */
    /*     kfree(cur->fs_struct->fdt); */
    /*     spinlock_cleanup(&(cur->fs_struct->file_lock)); */
    /*     kfree(cur->fs_struct); */
    /*     cur->fs_struct = NULL; */
    /*     return ret; */
    /* } */

    return 0;
}

void destroy_fd_table(struct proc* proc)
{
    KASSERT(proc != NULL);
    /* spinlock_acquire(&(proc->fs_struct->file_lock)); */
    struct fdtable* fdt = proc->fs_struct->fdt;
    __destroy_fdt(fdt, proc->fs_struct);
    /* spinlock_release(&(proc->fs_struct->file_lock)); */
    spinlock_cleanup(&(proc->fs_struct->file_lock));
    kfree(proc->fs_struct->fdt);
    kfree(proc->fs_struct);
    return;
}

