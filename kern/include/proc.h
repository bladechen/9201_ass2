/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _PROC_H_
#define _PROC_H_

/*
 * Definition of a process.
 *
 * Note: curproc is defined by <current.h>.
 */

#include <spinlock.h>
#include <bitmap.h>
#include <list.h>

struct addrspace;
struct thread;
struct vnode;
struct process_fd_table;

/*
 * Process structure.
 *
 * Note that we only count the number of threads in each process.
 * (And, unless you implement multithreaded user processes, this
 * number will not exceed 1 except in kproc.) If you want to know
 * exactly which threads are in the process, e.g. for debugging, add
 * an array and a sleeplock to protect it. (You can't use a spinlock
 * to protect an array because arrays need to be able to call
 * kmalloc.)
 *
 * You will most likely be adding stuff to this structure, so you may
 * find you need a sleeplock in here for other reasons as well.
 * However, note that p_addrspace must be protected by a spinlock:
 * thread_switch needs to be able to fetch the current address space
 * without sleeping.
 */
struct proc {
	char *p_name;			/* Name of this process */
	struct spinlock p_lock;		/* Lock for this structure */
	unsigned p_numthreads;		/* Number of threads in this process */

	/* VM */
	struct addrspace *p_addrspace;	/* virtual address space */

	/* VFS */
	struct vnode *p_cwd;		/* current working directory */

    struct files_struct* fs_struct;
/*
 * added by bladechen, the proc should be wrapped by proc_entry
 */
    struct proc_entry* controller;

    // only used for userlevel prag
    struct thread* kthread;





	/* add more material here as needed */
};
/*
 * added by bladechen
 *
 */

struct proc_entry
{

    atomic_t ref_count ;

    struct proc* process;
    struct list* children_list; /* the one in this list is this proc's child */

    struct list_head as_child_entry; /* this proc could also be other one's children*/

    pid_t pid;
    pid_t parent_pid;

    bool exit_flag;

    bool someone_wait;

    int exit_code;
    struct lock *process_lock;
    struct cv *process_cv;
};


#define MAX_PROCESS_COUNT 1024 /* the maximum process supported on OS161 system, TODO dynamic allocate the slot to support more? */
struct pidmap
{
    // struct spinlock relation_lock;
    struct spinlock pidmap_lock;
    atomic_t free_pid_slot;

    struct bitmap* pidmap;
    struct proc_entry* process_array[MAX_PROCESS_COUNT];
};


struct proc_entry* create_proc_entry(const char * );
bool try_destroy_proc_entry(struct proc_entry* );
struct proc_entry* get_proc_entry(pid_t pid);


void proc_attach_as_child(struct proc_entry* child, struct proc_entry* father);


void rearrange_children(struct proc_entry* entry); /* called while process exit, the children process should be put under kernel, or recycled */

/*
 * end, added by bladechen
 */

/* This is the process structure for the kernel and for kernel-only threads. */
extern struct proc *kproc;

/* Call once during system startup to allocate data structures. */
void proc_bootstrap(void);


void proc_shutdown(void);

/* Create a fresh process for use by runprogram(). */
struct proc *proc_create_runprogram(const char *name);

/* Destroy a process. */
void proc_destroy(struct proc *proc);

/* Attach a thread to a process. Must not already have a process. */
int proc_addthread(struct proc *proc, struct thread *t);

/* Detach a thread from its process. */
void proc_remthread(struct thread *t);

/* Fetch the address space of the current process. */
struct addrspace *proc_getas(void);

/* Change the address space of the current process, and return the old one. */
struct addrspace *proc_setas(struct addrspace *);
void proc_swapas(struct addrspace*);

struct proc* get_current_proc(void);

#endif /* _PROC_H_ */
