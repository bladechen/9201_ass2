#include <types.h>
#include <mips/atomic.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <thread.h>
#include <synch.h>
#include <vfs.h>
#include <fs.h>
#include <vnode.h>
#include <test.h>
#include "debug_print.h"
#include <fdtable.h>

#define NTHREADS 8

int fd_slot[200] = {0};

int fd_owner[200] = {0};

struct lock *fd_slot_lock = NULL;
struct semaphore *finished;

struct spinlock sp_lock;

int success = 0;


volatile int open_count = 0;
volatile int close_count = 0;

volatile int op_open_count = 0;
volatile int op_close_count = 0;



static void multi_open(void * argv1, unsigned long argv2)
{
    (void)argv1;
    (void) argv2;
    while (1)
    {
        int fd =  do_sys_open(-1, (void*)"kern_test_multi_open", O_CREAT, 0, get_current_proc()->fs_struct);
        if (fd >= 0)
        {
            mb_atomic_inc_int(&(fd_slot[fd]));
          int tmp =   mb_atomic_get_and_set_int(&fd_owner[fd], (int)argv2);
          if (tmp  != -1)
          {
              kprintf(RED "error: fd: %d is occupied by thread num: %d, mine is: %d!!" NONE,
                      fd, tmp, (int)argv2);
          }

            kprintf ("thread: %d, open fd: %d\n", (int)argv2, fd);
           /* lock_acquire(fd_slot_lock); */
           /* fd_slot[fd] ++; */
           /* lock_release(fd_slot_lock); */

        }
        else
        {
            kprintf ("thread: %d finished \n", (int)argv2);
            break;
        }
    }
    V(finished);

    return;
}


void test_open(void);
void test_open(void)
{

    kprintf ("begin test_open\n");
    memset(fd_slot, 0, sizeof(fd_slot));
    memset(fd_owner, -1, sizeof(fd_owner));
    membar();
    success = 1;

    for (int i = 0; i < NTHREADS; i ++)
    {
        int ret  = thread_fork("multi_open",
                                NULL,
                               &multi_open,
                               NULL,
                               i);
        if ( ret != 0)
        {
            panic("thread fork error:%s\n", strerror(ret));
        }

    }
    for (int i = 0; i < NTHREADS; i ++)
    {
        P(finished);
    }
    for (int i = 0; i < MAX_FD_COUNT_PER_PROCESS; i ++)
    {
        if (fd_slot[i] >= 2 )
        {
            kprintf (RED "error: fd: %d created more than one" NONE, i);
            success = 0;
        }
    }
    int total = 0;
    /* int flag = 0; */
    for (int i = 0; i < MAX_FD_COUNT_PER_PROCESS; i ++)
    {
        if (fd_owner[i] != -1)
        {
            total ++;
            kprintf ("fd %d: is occupied by (%d)\n", i, fd_owner[i]);

        }
        /* else */
        /* { */
        /*     kprintf(RED "fd %d is empty\n" NONE, i); */
        /* } */
        /*  */

    }
    if (success == 1)
    {
        kprintf(GREEN "passed test, total fd: %d\n" NONE, total);
    }
    kprintf ("finish test_open\n");
    for (int i = 3; i < MAX_FD_COUNT_PER_PROCESS;i ++)
    {
        if (fd_slot[i] != -1)
        {
            do_sys_close(i);
        }
    }
    return;
}

static void multi_open_close(void * argv1, unsigned long argv2)
{

    (void)argv1;
    (void) argv2;
    while (1)
    {
        if (argv2 >= 2)
        {
            /* if (mb_atomic_get_int(&open_count) == 0) */
            if (mb_atomic_cmpxchg_dec_to_target(&open_count, -1) )
            {
                kprintf ("bye, thread: %d, count: %d\n", (int)argv2, op_open_count);
                break;
            }
            int fd = 0;
continue_open_loop:
            fd = 0;
        /* int fd =  do_sys_open(-1, (void*)"kern_test_multi_open", O_CREAT, 0, get_current_proc()->fs_struct); */
            fd = do_sys_open(-1, (void*)"kern_test_multi_open_close", O_CREAT, 0, get_current_proc()->fs_struct);
            if (fd >= 0)
            {
                spinlock_acquire(&sp_lock);
                fd_slot[fd] ++;
                /* mb_atomic_inc_int(&(fd_slot[fd])); */
                int tmp =  mb_atomic_get_and_set_int(&fd_owner[fd], (int)argv2);
                if (tmp != -1)
                {
                    KASSERT(0);
                    kprintf(RED "error: fd: %d is occupied by thread num: %d, mine is: %d!!" NONE,
                            fd, tmp, (int)argv2);
                }
                op_open_count ++;
                spinlock_release(&sp_lock);
                kprintf ("thread: %d, open fd: %d\n", (int)argv2, fd);
                /* KASSERT(0); */

            }
            else
            {
                goto continue_open_loop;
            }
        }
        else
        {
            if (mb_atomic_cmpxchg_dec_to_target(&close_count, -1) )
            {
                break;
            }
            int fd = -1;
continue_close:
            fd = -1;
            spinlock_acquire(&sp_lock);
            for (int i = 0; i < MAX_FD_COUNT_PER_PROCESS;i ++)
            {
                if (fd_owner[i] != -1)
                {
                    fd = i;
                    fd_slot[i] -- ;
                    fd_owner[i] = -1;
                    op_close_count ++;
                    break;
                }
            }
            spinlock_release(&sp_lock);
            if (fd != -1)
            {
                KASSERT(do_sys_close(fd) == 0);
                kprintf ("thread: %d,  close fd: %d, count: %d, %d\n", (int)argv2, fd, op_close_count, close_count);

            }
            else
            {

                /* kprintf ("thread: %d, close fd: %d\n", (int)argv2, fd); */

                goto continue_close;
            }

        }
    }
    V(finished);

    return;
}


static void test_open_close(void)
{
    kprintf ("begin test_open_close\n");
    memset(fd_slot, 0, sizeof(fd_slot));
    memset(fd_owner, -1, sizeof(fd_owner));
    membar();
    success = 1;
    open_count = 100;
    op_open_count = 0;
    op_close_count = 0;
    close_count = 100;
    do_sys_close(5);
    do_sys_close(4);

    for (int i = 0; i < NTHREADS; i ++)
    {
        int ret  = thread_fork("multi_open_close",
                                NULL,
                               &multi_open_close,
                               NULL,
                               i);
        if ( ret != 0)
        {
            panic("thread fork error:%s\n", strerror(ret));
        }
    }
    for (int i = 0; i < NTHREADS; i ++)
    {
        P(finished);
    }
    for (int i = 0; i < MAX_FD_COUNT_PER_PROCESS; i ++)
    {
        if (fd_slot[i] >= 2 )
        {
            kprintf (RED "error: fd: %d created more than one" NONE, i);
            success = 0;
        }
    }
    int total = 0;
    /* int flag = 0; */
    for (int i = 0; i < MAX_FD_COUNT_PER_PROCESS; i ++)
    {
        if (fd_owner[i] != -1)
        {
            total ++;
            kprintf ("fd %d: is occupied by (%d)\n", i, fd_owner[i]);

        }
        /* else */
        /* { */
        /*     kprintf(RED "fd %d is empty\n" NONE, i); */
        /* } */
        /*  */

    }
    if (total == 0)
    {
        kprintf(GREEN "passed test, total fd: %d\n" NONE, total);
    }
    else
    {

        kprintf(RED "failed test, total fd: %d\n" NONE, total);
    }
    kprintf("open: %d, close: %d\n", op_open_count, op_close_count);
    kprintf ("finish test_open_close\n");

    return;
}

int file_multithread_test(int argc, char ** argv)
{
    (void) argc;
    (void) argv;
    finished = sem_create("finished", 0);
    if (finished == NULL)
    {
        panic("sem_create error\n");
    }

    fd_slot_lock = lock_create("fd_slot_lock");
    spinlock_init(&sp_lock);

    if (fd_slot_lock == NULL)
    {
        panic("lock_create error\n");
    }

    /* DEBUG_PRINT("enter file_multithread_test\n"); */
    /* test_open(); */
    test_open_close();
    sem_destroy(finished);
    lock_destroy(fd_slot_lock);
    spinlock_cleanup(&sp_lock);
    return 0;
}
