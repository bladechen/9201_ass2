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
#include <vfs.h>
#include <copyinout.h>

#include <list.h>
#include <current.h>
#include <fdtable.h>
#include <file.h>

oftlist *global_oft;

static oftnode * __create_node(struct vnode *vn)
{
    oftnode *n = kmalloc(sizeof(*n));
    KASSERT(n != NULL);
    struct list_head *head = kmalloc( sizeof(*head));
    KASSERT(head != NULL);
    n->vptr = vn;
    n->filepos = 0;
    n->refcount = 1;
    n->link_obj = head;
    return n;
}

// Initialise the global list for storing the open files
void oft_init(void)
{
    global_oft = kmalloc(sizeof(*global_oft));
    KASSERT(global_oft != NULL);
    INIT_LIST_HEAD(global_oft->head->link_obj);
}

int filp_open(int fd, const_userptr_t path, int flags, mode_t mode, int* retval)
{
    (void) mode;
    (void) fd;
    // do vfsopen
    int result;
    struct vnode *vn = NULL;

    result = vfs_open( (char *) path, flags, mode, &vn);
    if ( result )
    {
        // handle the open error
        *retval = result;
        return -1;
    }

    // If open is successful then
    // create node with the vnode pointer
    oftnode *newnode = __create_node(vn);

    // install into the global list
    spinlock_acquire((&global_oft->listlock));
    list_add(global_oft->head->link_obj, newnode->link_obj);
    spinlock_release((&global_oft->listlock));

    return 0;
}

//void __destroy_node (struct link_head list, struct link_head node);
//{
//    struct link_head temp;
    //list_for_each_safe(node, temp, list);
//}

// to destroy the list before quitting
void oft_destroy(void)
{
    // Check if the list is empty
    //
    //
    if( global_oft != NULL)
        kfree(global_oft);
}
