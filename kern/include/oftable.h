#ifndef _OFTABLE_H
#define _OFTABLE_H

#include <spinlock.h>
#include <vnode.h>
#include <types.h>

struct _oftnode {
    struct list_head link_obj;	/* link unit of intrusive list*/
    
    struct vnode *vptr;     // vnode access to file 
    off_t filepos;          // File position offset
    int refcount;           // Ref count for pointers to the same file 
    struct lock oftlock;    // Lock for synchronising refcount

};


#endif
