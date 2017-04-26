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
#include <filemacros.h>
#include <file.h>
#include <kern/iovec.h>

oftlist *global_oft;

static oftnode * __create_node(struct vnode *vn)
{
    oftnode *n = kmalloc(sizeof(*n));
    KASSERT(n != NULL);
    struct list_head *head = kmalloc(sizeof(*head));
    KASSERT(head != NULL);
    n->vptr = vn;
    n->filepos = 0;
    n->refcount = 1;
    n->link_obj = head;
    link_init((n->link_obj));
    n->filelock = lock_create("Lock for a file pointer in OFT");
    return n;
}

// Initialise the global list for storing the open files
void oft_init(void)
{
    global_oft = kmalloc(sizeof(*global_oft));
    KASSERT(global_oft != NULL);
    // do error check
    global_oft->lead = kmalloc(sizeof(*(global_oft->lead)));
    KASSERT(global_oft->lead != NULL);

    // do error check
    struct list *list_temp = global_oft->lead;
    KASSERT(global_oft != NULL);
    INIT_LIST_HEAD(&(list_temp->head));
}

int filp_open(int fd, const_userptr_t path, int flags, mode_t mode, int* retval, oftnode **nodeptr)
{
    (void) mode;
    (void) fd;
    // do vfsopen
    int result;
    struct vnode *vn = NULL;

    if ( fd == STDIN || fd == STDOUT || fd == STDERR )
    {
        char *console = kstrdup("con:");
        result = vfs_open(console, flags, mode, &vn);
        kfree(console);
    }
    else
    {
      // Either get vnode for 0,1 and 2
      result = vfs_open( (char *) path, flags, mode, &vn);
    } 
    // Error handling
    if ( result )
    {
        // handle the open error
        *retval = result;
        vfs_close(vn);
        return -1;
    }

    // If open is successful then
    // create node with the vnode pointer
    // to return
    *nodeptr = __create_node(vn);

    // if in append mode make the file position the offset of the end of file
    if ( flags & O_APPEND )
    {
        struct stat s;
        result = VOP_STAT((*nodeptr)->vptr, &s);
        if ( result )
        {
            *retval = result;
            return -1;
        }
        (*nodeptr)->filepos = s.st_size; 
    }
    // install into the global list
    spinlock_acquire((&global_oft->listlock));
    list_add_tail(&(global_oft->lead->head),((*nodeptr)->link_obj));
    spinlock_release((&global_oft->listlock));

    return 0;
}

ssize_t write_to_file(oftnode *node, void *kbuf, size_t nbytes, int *retval)
{
    struct uio u;
    struct iovec vec;
    // Set up uio struct
    uio_kinit(&vec, &u,(void *) kbuf, nbytes, node->filepos, UIO_WRITE);

    //kprintf("%s",kbuf);

    // call VOP_WRITE and pass it on
    //spinlock_acquire(&(node->oftlock));
    lock_acquire(node->filelock);
    off_t oldpos = node->filepos;
    int result = VOP_WRITE(node->vptr, &u);
    node->filepos = u.uio_offset;
    node->refcount--;

    if ( result )
    {
        // EFAULT	Part or all of the address space pointed to by buf is invalid
        *retval = result;
        lock_release(node->filelock);
        return -1;
    }
    *retval = node->filepos - oldpos;
    lock_release(node->filelock);
    return 0;
}

ssize_t read_from_file(oftnode *node, void * kbuf, size_t nbytes, int *retval)
{
    struct uio u;
    struct iovec vec;
    // Set up uio struct
    uio_kinit(&vec, &u,(void *) kbuf, nbytes, node->filepos, UIO_READ);

    // call VOP_READ and pass it on
    lock_acquire(node->filelock);
    off_t oldpos = node->filepos;
    int result = VOP_READ(node->vptr, &u);
    node->filepos = u.uio_offset;
    node->refcount--;

    if ( result )
    {
        // EFAULT	Part or all of the address space pointed to by buf is invalid
        *retval = result;
        lock_release(node->filelock);
        return -1;
    }
    *retval = node->filepos - oldpos;
    lock_release(node->filelock);
    return 0;
}
//void __destroy_node (struct link_head list, struct link_head node);
//{
//    struct link_head temp;
    //list_for_each_safe(node, temp, list);
    //lock_destroy(global_oft->filelock);
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
