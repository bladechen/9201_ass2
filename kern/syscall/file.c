#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <syscall.h>
#include <copyinout.h>

#include <list.h>
#include <file.h>

oftlist *global_oft;

// Initialise the global list for storing the open files
void oft_init(void)
{
    global_oft = kmalloc(sizeof(*global_oft));
    KASSERT(global_oft != NULL);
    INIT_LIST_HEAD(global_oft->head->link_obj);
}

// to destroy the list before quitting
void oft_destroy(void)
{
    // Check if the list is empty
    //
    //
    if( global_oft != NULL)
        kfree(global_oft);
}
