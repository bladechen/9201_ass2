
#include <kern/include/fdtable.h>
#include <lib.h>

fdtable * fdtable_init()
{
    // init fdtable datastructure
    fdtable *fdt;
    fdt = kmalloc(sizeof(*fdt));
    KASSERT(fdt!=NULL);

    // create spinlock
    spinlock_init(&(fdt->fdlock));

    // intialise the pointers to NULL
    fdt->fdesc[] = NULL;

    // Initialize bitmaps
    fdt->fdbitmap = bitmap_create(MAXFDTPROCESS);
    KASSERT(fdt->fdbitmap != NULL);
}

int fdtable_destroy(fdtable *fdt);
