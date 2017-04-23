/**
 * @file: process_syscall.c
 * @brief:  implementation of fork, getpid, ...
 * @author: bladechen
 *
 * 2017-1-6
 */

#include <types.h>
#include <lib.h>
#include <copyinout.h>
#include <syscall.h>
#include <kern/errno.h>
#include <kern/wait.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vfs.h>
#include <vnode.h>
#include <synch.h>
/* #include <mips/types.h> */
#include <mips/trapframe.h>
#include <limits.h>
#include "fdtable.h"

#include <debug_print.h>
#include <kern/fcntl.h>





static void fork_entry(void *tf, unsigned long nouse)
{
    KASSERT(tf != NULL);
    (void)nouse;

    struct trapframe tmp;
    struct trapframe *childframe = (struct trapframe *)tf;
    memcpy(&tmp, childframe, sizeof(struct trapframe));
    kfree(tf);

    tmp.tf_a3 = 0;
    tmp.tf_v0 = 0;
    tmp.tf_epc += 4;

    mips_usermode(&tmp);
    panic("fork_entry should not be here!!!!\n");
    return;
}


int syscall_fork(struct trapframe *tf, pid_t* retval)
{
    struct trapframe* child_tf = NULL;
    child_tf = kmalloc(sizeof(struct trapframe));
    if(child_tf == NULL)
    {
        *retval = ENOMEM;
        return -1;
    }
    /* pid_t parent_pid = get_current_proc()->controller->pid; */

    memcpy(child_tf, tf, sizeof(struct trapframe));
    struct proc_entry* p = create_proc_entry(NULL);
    if (p == NULL)
    {
        kfree(child_tf);
        *retval = ENOMEM;
        return -1;
    }
    /* p->parent_pid = parent_pid; */
    struct addrspace* child_addr = NULL;

    int ret = as_copy(proc_getas(), &child_addr);
    if (ret != 0)
    {
        try_destroy_proc_entry(p);
        kfree(child_tf);
        *retval = ret;
        return -1;
    }

    ret = copy_proc_fd_table(get_current_proc(), p->process);
    if (ret != 0)
    {
        try_destroy_proc_entry(p);
        kfree(child_tf);
        *retval = ret;
        return -1;

    }

    KASSERT(child_addr != NULL);
    KASSERT(p->process->p_addrspace == NULL);
    p->process->p_addrspace = child_addr;
    proc_attach_as_child(p, curthread->t_proc->controller);

    ret = thread_fork(curthread->t_name, p->process, fork_entry, (void*)child_tf, 0);
    if (ret != 0)
    {
        try_destroy_proc_entry(p);
        kfree(child_tf);
        *retval = ret;
        return -1;
    }
    *retval = p->pid;
    return 0;
}


int syscall_wait(pid_t pid,  userptr_t status, int options, int* retval)
{
    // The options argument should be 0. You are not required to implement any options. (However, your system should check to make sure that requests for options you do not support are rejected.) -- from os161 waitpid man page.
    (void) options;
    if (options != 0 )
    {

        *retval = EINVAL;
        return -1;
    }
    if (pid < 1 || pid >= MAX_PROCESS_COUNT)
    {
        *retval = ESRCH;
        return -1;
    }
    KASSERT(get_current_proc()->controller->exit_flag == 0);

    struct proc_entry* p = get_proc_entry(pid);

    if (p == NULL)
    {
        *retval = ESRCH;
        return -1;
    }
    if ( p->parent_pid != get_current_proc()->controller->pid)
    {
        try_destroy_proc_entry(p);
        *retval = ECHILD;
        return -1;
    }
    int tmp_code = 0;
    lock_acquire(p->process_lock);
    if (!(p->exit_flag))
    {
        p->someone_wait = true;
        cv_wait(p->process_cv, p->process_lock);
    }
    tmp_code = p->exit_code;
    lock_release(p->process_lock);

    /* called by kernel, status should be set NULL, otherwise from the user level, it should be a valid int pointer */
    if (status != NULL)
    {
        int result = copyout(&tmp_code, status, sizeof(userptr_t));
        if (result)
        {
            *retval = result;
            return -1;
        }
    }
    *retval = pid;
    try_destroy_proc_entry(p); // dec the ref inc by get_proc_entry

    try_destroy_proc_entry(p); // destroy the entry after waitpid

    return 0;

}
int syscall_exit(int exitcode, int exittype, int* retval)
{
    /* currently only support one thread per process in the user level, if called by kernel, it should obey this rule.
     * this function will never return, so .... */
    (void) retval;
    DEBUG_PRINT("syscall_exit\n");

    struct thread* cur_t = curthread;
    pid_t pid ;//= get_current_proc()->controller->pid;
    syscall_getpid(&pid);

    struct proc_entry* p = get_proc_entry(pid);
    KASSERT(p != NULL);
    KASSERT(p->exit_flag == 0);
    rearrange_children(p);

    pid_t parent_pid = p->parent_pid;

    lock_acquire(p->process_lock);
    if (parent_pid == 0 && p->someone_wait == 0)
    {
        proc_remthread(cur_t); /* the only thread attached to this proc, detach it from proc first, and destroy in thread_exit,
        it should be done here, because while destroying the proc, it will check whether there is a thread attaching to it */
        p->exit_flag = true;

        lock_release(p->process_lock);

        try_destroy_proc_entry(p); // dec ref which inc by get_proc_entry

        try_destroy_proc_entry(p); // actually free this proc entry
        thread_exit();
        KASSERT(0);
    }

    if (exittype  == __WEXITED)
    {
        p->exit_code = _MKWAIT_EXIT(exitcode);
    }
    else if (exittype == __WSIGNALED)
    {
        p->exit_code = _MKWAIT_SIG(exitcode);
    }
    else if (exittype == __WCORED)
    {
        p->exit_code = _MKWAIT_CORE(exitcode);
    }
    else
    {
        /* unknowed type, must not be here*/
        KASSERT(0);
    }

    p->exit_flag = true;

    cv_signal(p->process_cv, p->process_lock);

    proc_remthread(cur_t);
    lock_release(p->process_lock);

    try_destroy_proc_entry(p);

    thread_exit();
    KASSERT(0);
    return 0;
}

int syscall_getpid(pid_t* retval)
{
    *retval = get_current_proc()->controller->pid;
    KASSERT(*retval >= 0);
    return 0;
}


static int kexecv(char* prog, int argc, char** args)
{
    struct addrspace *as;
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    int result;

    /*
     * copied from runprogram, there are 2 differences
     * 1. there should be old proc addrspace which need to be destroyed, and create new one.
     * 2. args should be stored in somewhere for the newly starting user proc to get, here i choose the user stack.
     */

    /* Open the file. */
    result = vfs_open(prog, O_RDONLY, 0, &v);
    if (result) {
        return result;
    }

    /* We should not be a new process.
     * destroy the old addrspace , and create new one*/
    KASSERT(proc_getas() != NULL);

    /* Create a new address space. */
    as = as_create();
    if (as == NULL) {
        vfs_close(v);
        return ENOMEM;
    }

    /* Switch to it and activate it. */
    struct addrspace* old =  proc_setas(as);
    /* if (old != NULL) */
    /* { */
    /*     as_destroy(old); */
    /*     old = NULL; */
    /* } */
    as_activate();
    /* Load the executable. */
    result = load_elf(v, &entrypoint);
    if (result) {
        /* p_addrspace will go away when curproc is destroyed */
        vfs_close(v);
        proc_swapas(old);
        return result;
    }
    vfs_close(v);

    /* Define the user stack in the address space */
    result = as_define_stack(as, &stackptr);
    if (result) {
        proc_swapas(old);
        /* p_addrspace will go away when curproc is destroyed */                                                              return result;
    }

    size_t user_stack = (size_t) stackptr;

    // one more slot for NULL
    char** tmp_argv = kmalloc(sizeof (char* ) * (argc + 1));
    if (tmp_argv == NULL)
    {
        proc_swapas(old);
        return ENOMEM;
    }
    for (int i = 0; i < argc; i ++)
    {
        int arg_len  = strlen(args[i]) + 1;
        user_stack -= arg_len;

        // aligin by 4
        if (arg_len % 4 != 0)
        {
            int need_align = 4 - arg_len % 4;
            user_stack -= need_align;
            for (int i = 0; i < need_align; i ++)
            {
                ((char*)(user_stack))[arg_len + i] = 0;
            }
        }
        result = copyout(args[i],(userptr_t)user_stack, arg_len);
        if ( result != 0)
        {
            proc_swapas(old);
            kfree(tmp_argv);
            return result;
        }

        tmp_argv[i] = (char*) user_stack;

    }
    tmp_argv[argc] = NULL;

    user_stack -= (argc + 1) * sizeof(char*);
    result = copyout(tmp_argv, (userptr_t)user_stack,  (argc + 1) * sizeof(char*));
    if (result != 0)
    {
        proc_swapas(old);
        kfree(tmp_argv);
        return result;
    }

    kfree(tmp_argv);
    // not return to the caller, these two pointer should be free before entering new proc
    kfree(args);
    kfree(prog);
    if (old != NULL)
    {
        as_destroy(old);
    }


    /* Warp to user mode. */
    enter_new_process(argc /*argc*/, (userptr_t)user_stack/*userspace addr of argv*/,
                      NULL /*userspace addr of environment*/,
                      (vaddr_t)user_stack , entrypoint);

    /* enter_new_process does not return. */
    panic("enter_new_process returned\n");
    return 0;
}
/* int read_one_byte(userptr_t* args, ) */
/* { */
/*     return 0; */
/*  */
/* } */
int syscall_execv(const_userptr_t program, userptr_t* args, int* retval)
{
    /*
     * checking parameters
     */
    *retval = 0;
    if (program == NULL)
    {
        *retval = EFAULT;
        return -1;

    }
    if (args == NULL)
    {
        *retval = EFAULT;
        return -1;
    }

    /*
     * end checking
     * begin init kernel val
     */

    char * kprog = (char *)kmalloc(sizeof(char)  * NAME_MAX);
    if (kprog == NULL)
    {
        *retval = ENOMEM;
        return -1;
    }
    size_t kprog_len = NAME_MAX;

    int ret = copyinstr(program, kprog, NAME_MAX, &kprog_len);
    if (ret != 0)
    {
        *retval = -ret;
        kfree(kprog);
        return -1;
    }

    char just_tmp = 'a';
    int  argc = 0;
    ret = copyin((const_userptr_t)args, &just_tmp, 1);
    if (ret != 0)
    {
        *retval = -ret;
        DEBUG_PRINT("invalid user args pointer\n");
        kfree(kprog);
        return -1;
    }
    while (1)
    /* while (args[argc++] != (userptr_t)0x0) */
    {
/* int copyin(const_userptr_t usersrc, void *dest, size_t len); */
        ret = copyin(args [ argc ], &just_tmp, 1);


        if (ret != 0 && args [ argc ] == NULL)
        {
            DEBUG_PRINT("invalid user args pointer haha %d %d %d\n", argc, ret, just_tmp);
            argc ++;
            break;
        }
        else if (ret != 0)
        {
            *retval = -ret;
            return -1;
        }
        argc ++;
        /* if (just_tmp == 0) */
        /* { */
        /*     break; */
        /* } */


    }
    DEBUG_PRINT("argc %d\n", argc);
    argc  =  argc > 0 ?argc - 1: 0 ;


    char** kargs = (char **) kmalloc(sizeof(char*) * argc);
    if (kargs == NULL)
    {
        *retval = ENOMEM;
        return -1;
    }
    memset(kargs, 0, sizeof (char*) * argc);

    for (int i = 0; i < argc; i ++)
    {

        int args_len = 0;

        while (true)
        {
            args_len ++ ;
            if (args_len > ARG_MAX )
            {
                *retval = ENAMETOOLONG;
                goto execv_err;
            }
            if (*(char*)(args[i] +args_len) == '\0')
            {
                break;
            }
        }
        kargs[i] = kmalloc(sizeof(char) * (args_len + 1));
        size_t tmp_len = 0;
        ret = copyinstr(args[i], kargs[i], args_len + 1, &tmp_len);
        if (ret != 0)
        {
            *retval = ret;
            goto execv_err;
        }
    }

    // kexecv is responsible for destroying kprog and kargs if successfully creating new proc
    *retval = kexecv(kprog, argc, kargs);

execv_err:
    if (kprog != NULL)
    {
        kfree(kprog);
        kprog = NULL;
    }
    if (kargs != NULL)
    {
        for (int i = 0; i < argc; i++)
        {
            if (kargs[i] != NULL)
            {
                kfree(kargs[i]);
            }
            else
            {
                break;
            }
        }
        kfree(kargs);
        kargs = NULL;
    }
    return ( *retval ) == 0 ? 0: -1;
}
