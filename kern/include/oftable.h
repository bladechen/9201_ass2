#ifndef _OFTABLE_H
#define _OFTABLE_H

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
};

typedef struct _oftlist oftlist;
// Global list of file 
struct _oftlist {
    oftnode *head;
    struct lock *listlock;
}
#endif
