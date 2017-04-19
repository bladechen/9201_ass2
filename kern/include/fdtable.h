/*
    File table data structure declaration
 */
#ifndef _FDTABLE_H
#define _FDTABLE_H

#define MAXFDTPROCESS 32

#include <spinlock.h>
#include <bitmap.h>
#include <oftable.h>
#include <bool.h>

typedef struct _fdtable {
    //needs to be a pointer to the node type of the linked list of OF tables
    struct oftnode *fdesc[MAXFDTPROCESS];   // Pointers to 
    mode_t fileperms[MAXFDTPROCESS];
    struct bitmap *fdbitmap;
    struct spinlock fdlock;
}fdtable;

int fdtable_init();
int fdtable_destroy(fdtable *fdt);
int fdtable_allocate();
bool isfdfree();

#endif
