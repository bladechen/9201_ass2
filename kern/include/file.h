/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>
#include <spinlock.h>
#include <synch.h>
#include <vnode.h>
#include <types.h>

#include "list.h"

typedef struct _oftnode oftnode;
struct _oftnode {

    struct vnode *vptr;         // vnode access to file 
    off_t filepos;              // File position offset
    int refcount;               // Ref count for pointers to the same file 
    struct spinlock oftlock;    // Lock for synchronising refcount

    struct list_head *link_obj;	/* link unit of intrusive list*/
};

typedef struct _oftlist oftlist;
// Global list of file 
struct _oftlist {
    struct list *lead;
    struct spinlock listlock;
};

// Initialise the global list for storing the open files
void oft_init(void);
// Allocate node, add to global list
int filp_open(int fd, const_userptr_t path, int flags, mode_t mode, int* retval, oftnode **nodeptr);
// to destroy the list before quitting
void oft_destroy(void);

#endif /* _FILE_H_ */
