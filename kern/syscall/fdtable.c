
#include <fdtable.h>
#include <lib.h>
#include <kern/errno.h>
#include <proc.h>
#include <current.h>
static struct proc * getcurproc(void)
{
  return curproc;
}

fdtable* fdtable_init(void)
{
    // init fdtable datastructure
    fdtable* fdt = kmalloc(sizeof(*fdt));
    if ( fdt == NULL) {
        return NULL;
    }
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
    if ( fdt->fdbitmap == NULL ) {
        return NULL;
    }

    // init spinlock
    spinlock_init(&(fdt->fdlock));

    return fdt;
}

void fdtable_destroy(fdtable *fdt)
{
    if ( fdt == NULL )
    {
        return;
    }

    if ( fdt->fdbitmap != NULL) {
    // Destroy bitmap
    bitmap_destroy(fdt->fdbitmap);
    }

     // clean up spinlock
    spinlock_cleanup(&(fdt->fdlock));
}


static int get_unused_fd(fdtable *fdt)
{
    KASSERT(fdt->fdbitmap != NULL);

    // Allocate the next avaiable file descriptor
    int i;
    for (i = 3; i<MAXFDTPROCESS; i++)
    {
        spinlock_acquire(&(fdt->fdlock));
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
    return EMFILE;
}

int do_sys_open(const_userptr_t path, int flags, mode_t mode, int* retval)
{
    // Acquire lock
    (void )path;
    (void )flags;
    (void )mode;
    (void )retval;
    
    int result;
    struct proc *cp = getcurproc();
    result = get_unused_fd(cp->fdt); 

    if ( result )
    {
        *retval = result;
        return -1;
    }
    return 0;
}


