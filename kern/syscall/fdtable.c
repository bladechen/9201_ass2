
#include <fdtable.h>
#include <lib.h>
#include <kern/errno.h>

fdtable* fdtable_init(void)
{
    // init fdtable datastructure
    fdtable* fdt = kmalloc(sizeof(*fdt));
    KASSERT(fdt!=NULL);
    int i = 0;
    for (;i<MAXFDTPROCESS;i++)
    {
        // intialise the pointers to NULL
        fdt->fdesc[i] = NULL;
        // Set file perms to 0
        fdt->fileperms[i] = 0;
    }

    // Initialize bitmaps
    fdt->fdbitmap = bitmap_create(MAXFDTPROCESS);
    KASSERT(fdt->fdbitmap != NULL);

    // init spinlock
    spinlock_init(&(fdt->fdlock));

    return fdt;
}

void fdtable_destroy(fdtable *fdt)
{
    // Destroy bitmap
    bitmap_destroy(fdt->fdbitmap);

     // clean up spinlock
    spinlock_cleanup(&(fdt->fdlock));
}

//int fdtable_allocate(fdtable *fdt)
//{
//    // Allocate the next avaiable file descriptor
//    int i;
//    for (i = 3; i<MAXFDTPROCESS; i++)
//    {
//        spinlock_acquire(fdt->fdlock);
//        // acquire lock for fdtable
//        if( bitmap_isset(fdt->fdbitmap, i) )
//        {
//            // set the bitmap
//            // initilize the values for open
//            spinlock_release(&(fdt->fdlock));
//            return i;
//        }
//        spinlock_release(&(fdt->fdlock));
//    }
//    // Return error if all the tables are used
//    return -1;
//}

int fd_open(fdtable *fdt,int fd)
{
    KASSERT(fd<MAXFDTPROCESS);
    KASSERT(fdt != NULL);
    KASSERT(fdt->fdbitmap != NULL);

    // Allocate the next avaiable file descriptor
    int i;
    for (i = 3; i<MAXFDTPROCESS; i++)
    {
        spinlock_acquire(fdt->fdlock);
        // acquire lock for fdtable
        if( bitmap_isset(fdt->fdbitmap, i) )
        {
            // set the bitmap
            // initilize the values for open
            spinlock_release(&(fdt->fdlock));
            return i;
        }
        spinlock_release(&(fdt->fdlock));
    }
    // Return error if all the tables are used
    return -1;
    // Check bitmap
    if ( bitmap_isset(fdt->fdbitmap) )
    {

    }

    if ( fdesc[fd] != NULL)
        return true;
    else
        return false;
}
