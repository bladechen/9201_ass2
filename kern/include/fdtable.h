/*
    File table data structure declaration
 */
#ifndef _FDTABLE_H
#define _FDTABLE_H

#define MAXFDTPROCESS 32

#include <types.h>
#include <spinlock.h>
#include <bitmap.h>
#include <file.h>

typedef struct _fdtable fdtable;
struct _fdtable {
    //needs to be a pointer to the node type of the linked list of OF tables
    struct oftnode *fdesc[MAXFDTPROCESS];   // Pointers to the nodes of oft
    int fileperms[MAXFDTPROCESS];
    struct bitmap *fdbitmap;
    struct spinlock fdlock;
};

fdtable* fdtable_init(void);
void fdtable_destroy(fdtable *fdt);
int do_sys_open(const_userptr_t path, int flags, mode_t mode, int* retval);

#endif
