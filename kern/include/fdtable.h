#ifndef _FDTABLE_H
#define _FDTABLE_H

#define MAXFDPROCESS 32

#include <spinlock.h>
#include <bitmap.h>
#include <fdtable.h>

typedef struct _fdtable {
    //needs to be a pointer to the node type of the linked list of OF tables
    globalOFtable *fdesc[MAXFDPROCESS];
    struct bitmap *fdbitmap;
    struct lock *fdlock;
}fdtable;

fdtable_init();
fdtable_destroy();
int fdtable_allocate();

#endif
