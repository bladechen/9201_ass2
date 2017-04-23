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

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <mips/atomic.h>
#include "debug_print.h"

#include <fdtable.h>
/* #include <file_table.h> */

/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

struct pidmap *g_pidmap = NULL;

struct lock* proc_lock = NULL;

struct spinlock  relation_lock = SPINLOCK_INITIALIZER;


static void inc_proc_entry_ref(struct proc_entry* e)
{
    mb_atomic_inc_int(&e->ref_count);

}


static void destroy_proc_entry(struct proc_entry* entry);
static bool dec_proc_entry_ref(struct proc_entry* e)
{
    int tmp = 0;
    bool ret = mb_atomic_cmpxchg_dec_to_target(&(e->ref_count), 0);
    tmp = e->ref_count;
    DEBUG_PRINT("pid: %d dec ref to: %d.\n" ,e->pid,  tmp);
    if (!ret)
    {
        return false;
    }
    else
    {
        /* no other ref to it, destroy it!! */
        destroy_proc_entry(e);
    }
    return true;

}

bool try_destroy_proc_entry(struct proc_entry* e)
{
    bool ret = dec_proc_entry_ref(e);
    return ret;
}

/*
 * negative return indicates error happen, the return value is -errno
 * otherwise, it is the allocated pid.
 */

static pid_t alloc_pid(void)
{
    pid_t ret = 0;
    if (mb_atomic_get_int(&(g_pidmap->free_pid_slot)) == 0)
    {
        return -ENPROC;
    }
    spinlock_acquire(&(g_pidmap->pidmap_lock));
    int tmp = mb_atomic_cmpxchg_dec_to_target(&(g_pidmap->free_pid_slot), 0);
    if (tmp)
    {
        spinlock_release(&(g_pidmap->pidmap_lock));
        return -ENPROC;
    }
    unsigned index = 0;
    tmp = bitmap_alloc(g_pidmap->pidmap, &index);
    if (tmp != 0)
    {
        spinlock_release(&(g_pidmap->pidmap_lock));
        return (tmp > 0) ? (-tmp): tmp;
    }
    ret = (pid_t) index;
    KASSERT(g_pidmap->process_array[index] == NULL);
    spinlock_release(&(g_pidmap->pidmap_lock));
    return ret;
}

static void install_proc_entry(pid_t pid, struct proc_entry* e)
{
    KASSERT(pid >= 0);
    KASSERT(e != NULL);


    // bitmap does not count for the ref.
    KASSERT(e->ref_count >= 1);

    KASSERT(bitmap_isset(g_pidmap->pidmap, pid));
    e->pid = pid;
    g_pidmap->process_array[pid] =  e;
}

/*
 * the second parameter is only used for checking purpose
 */
static void dealloc_pid(pid_t pid, struct proc_entry* e)
{
    struct proc_entry* tmp;
    KASSERT(pid >= 0);
    spinlock_acquire(&(g_pidmap->pidmap_lock));
    if (!bitmap_isset(g_pidmap->pidmap, pid) )
    {
        DEBUG_PRINT("why pid: %d, proc_entry: %p, bitmap is empty\n", pid, e);
        KASSERT(g_pidmap->process_array[pid] == NULL);
        spinlock_release(&(g_pidmap->pidmap_lock));
        return;
    }
    bitmap_unmark(g_pidmap->pidmap, pid);
    mb_atomic_inc_int(&(g_pidmap->free_pid_slot));
    KASSERT(g_pidmap->free_pid_slot <= MAX_PROCESS_COUNT);
    tmp = g_pidmap->process_array[pid];
    g_pidmap->process_array[pid] = NULL;
    spinlock_release(&(g_pidmap->pidmap_lock));
    if (e != NULL)
        KASSERT(tmp == e);
    return;
}

static void destroy_pidmap(void)
{
    if (g_pidmap == NULL)
    {
        return;
    }
    KASSERT(g_pidmap->free_pid_slot == MAX_PROCESS_COUNT);
    if (g_pidmap->pidmap != NULL)
    {
        bitmap_destroy(g_pidmap->pidmap);
        g_pidmap->pidmap = NULL;
    }
    spinlock_cleanup(&(g_pidmap->pidmap_lock));
    kfree(g_pidmap);
    g_pidmap = NULL;
    return;
}


static int init_pidmap()
{
    KASSERT(g_pidmap == NULL);
    g_pidmap = kmalloc(sizeof(*g_pidmap));
    if (g_pidmap == NULL)
    {
        return -1;
    }



    spinlock_init(&(g_pidmap->pidmap_lock));
    g_pidmap->pidmap = NULL;
    g_pidmap->free_pid_slot = MAX_PROCESS_COUNT;
    g_pidmap->pidmap = bitmap_create(MAX_PROCESS_COUNT);
    for (int i = 0; i < MAX_PROCESS_COUNT; i ++)
    {
        g_pidmap->process_array[i] = NULL;
    }
    if (g_pidmap->pidmap == NULL)
    {
        destroy_pidmap();
        return -1;
    }
    proc_lock = lock_create("global proc lock");
    if (proc_lock == NULL)
    {
        destroy_pidmap();
        return -1;
    }
    return 0;

}




static struct proc_entry* init_proc_entry(void)
{
    struct proc_entry* entry = kmalloc(sizeof(struct proc_entry));
    if ( entry == NULL)
    {
        return NULL;
    }
    link_init(&entry->as_child_entry);
    entry->pid = -1;
    entry->parent_pid = 0; /* default is under the kernel proc */
    entry->exit_flag = 0;
    entry->ref_count = 1;
    entry->someone_wait = 0;
    entry->exit_code = 0;
    entry->children_list = NULL;
    entry->process_lock = NULL;
    entry->process_cv = NULL;
    entry->process = NULL;


    entry->children_list = init_list(offsetof(struct proc_entry, as_child_entry));
    if (entry->children_list == NULL)
    {
        destroy_proc_entry(entry);
        return NULL;
    }
    entry->process_lock = lock_create("process_lock");
    if (entry->process_lock == NULL)
    {
        destroy_proc_entry(entry);
        return NULL;
    }
    entry->process_cv = cv_create("process_cv");
    if (entry->process_cv == NULL)
    {
        destroy_proc_entry(entry);
        return NULL;
    }
    return entry;
}

void proc_attach_as_child(struct proc_entry* child, struct proc_entry* father)
{
    KASSERT(child != NULL);
    KASSERT(father != NULL);

    spinlock_acquire(&(relation_lock));
    KASSERT(!is_linked(&child->as_child_entry));
    list_insert_tail(father->children_list, child);
    KASSERT(father->pid >= 0);
    child->parent_pid = father->pid;

    spinlock_release(&(relation_lock));
    return;
}

static void proc_recycle_child(struct proc_entry* child)
{
    /* KASSERT(spinlock_do_i_hold(&(g_pidmap->relation_lock))); */
    KASSERT(child != NULL);
    KASSERT(child->pid >= 1);
    KASSERT(!(is_linked(&(child->as_child_entry))));

    /* kernel is shuting down, should destroy entry list via proc_shutdown()*/
    if (child->parent_pid == 0)
    {
        KASSERT(0);
    }

    if (child->exit_flag == 0) /* still running, place it under kernel(0) process*/
    {
        DEBUG_PRINT("place pid: %d under kernel\n", child->pid);
        spinlock_acquire(&(relation_lock));
        list_insert_tail(kproc->controller->children_list, child);
        spinlock_release(&(relation_lock));
        child->parent_pid = 0;
    }
    else /* zombie proc should be cleared while parent exit */
    {
        DEBUG_PRINT("clear zombie with pid: %d \n", child->pid);

        try_destroy_proc_entry(child);
    }
    return;
}

/*
 */
struct proc_entry* create_proc_entry(const char* name)
{
    struct proc* newproc  = proc_create_runprogram(name);
    if (newproc  == NULL)
    {
        return NULL;
    }

    pid_t pid = alloc_pid();
    if (pid < 0) {
        proc_destroy(newproc);
        return NULL;
    }

    struct proc_entry* e = init_proc_entry();
    if (e == NULL)
    {
        proc_destroy(newproc);
        dealloc_pid(pid, NULL);
        return NULL;
    }

    e->process = newproc;
    newproc->controller = e;
    e->parent_pid = 0;
    install_proc_entry(pid, e);
    KASSERT(e->ref_count == 1);
    return e;
}

void rearrange_children(struct proc_entry* entry)
{
    struct proc_entry *child = NULL;
    struct list_head tmp;

    struct proc_entry** tmp_pid = kmalloc(sizeof (struct proc_entry*) * MAX_PROCESS_COUNT);
    int count = 0;

    lock_acquire(proc_lock);
    spinlock_acquire(&(relation_lock));
    list_for_each_entry_safe(child, tmp, entry->children_list)
    {
        KASSERT(child != NULL);
        link_detach(child, as_child_entry);
        KASSERT(child->parent_pid == entry->pid);
        tmp_pid[count++] = child;
        inc_proc_entry_ref(child);
    }

    KASSERT(is_list_empty(entry->children_list) == 1);
    spinlock_release(&(relation_lock));

    for (int i = 0; i < count; i++)
    {
        proc_recycle_child(tmp_pid[i]);
        try_destroy_proc_entry(tmp_pid[i]);
    }

    lock_release(proc_lock);
    kfree(tmp_pid);
    return;
}

static void destroy_proc_entry(struct proc_entry* entry)
{
    if (entry == NULL)
    {
        return;
    }

    bool i_lock = 0;

    if (lock_do_i_hold(proc_lock) == 0)
    {
        lock_acquire(proc_lock);
        i_lock = 1;
    }

    if (entry->ref_count >= 1)
    {

        DEBUG_PRINT("proc entry: %p, pid: %d fail in destroy, ref: %d\n", entry, entry->pid, entry->ref_count);
        if (i_lock == 1) lock_release(proc_lock);
        return;
    }

    KASSERT(entry->ref_count == 0);
    DEBUG_PRINT("proc entry: %p, pid: %d destroyed\n", entry, entry->pid);
    if (entry->pid >= 0 )
    {
        /* no other one refers to this entry but the bitmap if go here*/
        dealloc_pid(entry->pid, entry);
    }

    spinlock_acquire(&relation_lock);
    if (is_linked(&(entry->as_child_entry)))
        link_detach(entry, as_child_entry);

    spinlock_release(&relation_lock);

    if (entry->children_list != NULL)
    {
        KASSERT(is_list_empty(entry->children_list) == 1);
        destroy_list(entry->children_list);
        entry->children_list = NULL;
    }
    if (entry->process_cv != NULL)
    {
        cv_destroy(entry->process_cv);
        entry->process_cv = NULL;
    }
    if (entry->process_lock != NULL)
    {
        lock_destroy(entry->process_lock);
        entry->process_lock = NULL;
    }
    if (entry->process != NULL)
    {
        proc_destroy(entry->process);
        entry->process = NULL;
    }
    kfree(entry);
    if (i_lock == 1) lock_release(proc_lock);

    return;
}

struct proc_entry* get_proc_entry(pid_t pid)
{
    if (pid < 0 || pid >= MAX_PROCESS_COUNT)
    {
        return NULL;
    }
    struct proc_entry* ret = NULL;
    lock_acquire(proc_lock);
    spinlock_acquire(&(g_pidmap->pidmap_lock));
    if (bitmap_isset(g_pidmap->pidmap, pid) == 0)
    {
        spinlock_release(&(g_pidmap->pidmap_lock));
        lock_release(proc_lock);
        return ret;
    }
    if (g_pidmap->process_array[pid] == NULL)
    {
        spinlock_release(&(g_pidmap->pidmap_lock));
        lock_release(proc_lock);
        return ret;
    }
    ret = g_pidmap->process_array[pid];
    inc_proc_entry_ref(ret);
    spinlock_release(&(g_pidmap->pidmap_lock));
    lock_release(proc_lock);
    return ret;
}



/*
 * Create a proc structure.
 */
static
struct proc *
proc_create(const char *name)
{
    DEBUG_PRINT("create new proc_create\n");
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
    proc->kthread = NULL;
	/* proc->p_name = kstrdup(name); */
	/* if (proc->p_name == NULL) { */
	/* 	kfree(proc); */
	/* 	return NULL; */
	/* } */
    if (name  == NULL)
    {
        proc->p_name = NULL;

    }
    else
    {
        proc->p_name = kstrdup(name);
        if (proc->p_name == NULL) {
            kfree(proc);
            return NULL;
        }
    }


	proc->p_numthreads = 0;
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

    int ret = init_fd_table(proc);
    if (ret != 0)
    {
        spinlock_cleanup(&proc->p_lock);
        kfree(proc->p_name);
        kfree(proc);
        return NULL;
    }


	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_destroy(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);


    DEBUG_PRINT("destroy fd table\n");
    destroy_fd_table(proc);
	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	KASSERT(proc->p_numthreads == 0);
	spinlock_cleanup(&proc->p_lock);

	kfree(proc->p_name);
	kfree(proc);
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
   int ret = init_pidmap();
    if (ret != 0) {
        panic("pidmap init failed\n");
    }

	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}
    pid_t pid = alloc_pid();
    if (pid < 0)
    {
        panic("pid(0) allocated error for the kernel proc\n");
    }

    KASSERT(pid == 0);
    struct proc_entry* entry = init_proc_entry();
    if (entry == NULL)
    {
        panic("alloc kernel proc entry failed\n");
    }

    kproc->controller = entry;

    entry->process = kproc;
    entry->parent_pid = -1;
    install_proc_entry(pid, entry);

}

void proc_shutdown(void)
{
    /*
     * 0 is the kernel proc, all the other thread should be halt. no race issue here.
     * should be only one thread running this while no other ones excuting
     */
    /* return; */
    for (int i = 0; i < MAX_PROCESS_COUNT; i ++)
    {
        struct proc_entry* p = g_pidmap->process_array[i];
        if (p == NULL)
        {
            continue;
        }
        /* p->process->p_numthreads = 0; // thread should be cleared previously, so just set it to zero */
        make_list_empty(p->children_list);
        /* destroy_proc_entry(p); */
    }



    /* for (int i = 1; i < MAX_PROCESS_COUNT;i ++) */
    /* { */
    /*     struct proc_entry* p = get_proc_entry(i); */
    /*     // grandson process may still running, force kill them. */
    /*             #<{(| (void)p; |)}># */
    /*     if (p != NULL) */
    /*     { */
    /*         if (p->process != NULL) */
    /*             p->process->p_numthreads = 0; */
    /*             #<{(| proc_remthread(p->process->kthread); |)}># */
    /*  */
    /*         while (try_destroy_proc_entry(p) == false){}; */
    /*     } */
    /* } */
    /*  */

    dealloc_pid(0, kproc->controller);
    // TODO destroy kernel proc, may cause error while delteing the kern f table
    g_pidmap->free_pid_slot = MAX_PROCESS_COUNT;
    destroy_pidmap();
    lock_destroy(proc_lock);
    return;

}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *newproc;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}
    init_stdio(newproc->fs_struct);
    /* setup_process_stdio(newproc); */

	/* VM fields */

	newproc->p_addrspace = NULL;

	/* VFS fields */

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	return newproc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	proc->p_numthreads++;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = proc;
	splx(spl);

	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
    if (t == NULL)
        return;
	struct proc *proc;
	int spl;

	proc = t->t_proc;
    if (proc == NULL)
    {
        return;
    }
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	KASSERT(proc->p_numthreads > 0);
	proc->p_numthreads--;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = NULL;
	splx(spl);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

struct proc* get_current_proc()
{
    KASSERT( curthread->t_proc != NULL );
    return  curthread->t_proc;

}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}
/* struct proc* get_current_proc(); */
/*
 * added by bladechen,
 * if success, the old one will be destroyed
*/
void proc_swapas(struct addrspace* new)
{
    KASSERT(new != NULL);
    struct addrspace * old = proc_setas(new);
    if (old != NULL)
    {
        as_destroy(old);
    }
    as_activate();
}


