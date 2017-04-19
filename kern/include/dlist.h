
#ifndef _DLIST_H
#define _DLIST_H

#include <types.h> 
#include <vnode.h>

struct _dnode {
    dnode *prev; // A reference to the previous node
    dnode *next; // A reference to the next node
    struct data; // Data or a reference to data
}dnode;

struct record DoublyLinkedList {
    dnode *head;   // points to first node of list
    dnode *tail;    // points to last node of list
    int count;
}

struct data {
    struct vnode *vp;
    off_t filepos;
}
#endif
