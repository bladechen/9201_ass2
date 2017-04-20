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
    struct list_head link_obj;	/* link unit of intrusive list*/

    struct vnode *vptr;     // vnode access to file 
    off_t filepos;          // File position offset
    int refcount;           // Ref count for pointers to the same file 
    struct spinlock oftlock;    // Lock for synchronising refcount

    oftlist *owner;
};

typedef struct _oftlist oftlist;

// Global list of file 
struct _oftlist {
    oftnode *head;
    struct spinlock listlock;
};
#endif /* _FILE_H_ */
