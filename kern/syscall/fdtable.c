
#include <fdtable.h>
#include <lib.h>
#include <kern/errno.h>
#include <proc.h>
#include <current.h>
#include <file.h>

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


static int get_unused_fd(fdtable *fdt, int *fd)
{
    KASSERT(fdt->fdbitmap != NULL);

    // Allocate the next avaiable file descriptor
    int i;
    for (i = 0; i<MAXFDTPROCESS; i++)
    {
        // acquire lock for fdtable
        spinlock_acquire(&(fdt->fdlock));
        // if the bit is not set then choose it
        if( bitmap_isset(fdt->fdbitmap, i) == 0 )
        {
            // initilize the values for open
            *fd = i;
            // set the bitmap;
            bitmap_mark(fdt->fdbitmap, i);
            spinlock_release(&(fdt->fdlock));
            return 0;
        }
        spinlock_release(&(fdt->fdlock));
    }
    // Return error if all the tables are used
    return EMFILE;
}

static void put_fd(fdtable *fdt, int fd)
{
    if ( fd<0 || fd >= MAXFDTPROCESS )
    {
        kprintf("Invalid fd number to put back");
        return;
    }

    if ( bitmap_isset(fdt->fdbitmap, fd) != 0)
    {
        bitmap_unmark(fdt->fdbitmap,fd);
        fdt->fileperms[fd] = 0; 
    }
}

int do_sys_open(const_userptr_t path, int flags, mode_t mode, int* retval)
{
    // Acquire lock
    (void )path;
    (void )flags;
    (void )mode;
    (void )retval;

    // Make sure path is not NULL
    int result;
    struct proc *cp = getcurproc();
    int fd;
    // Get unused fd
    result = get_unused_fd(cp->fdt, &fd); 

    // proceed to get unused FD
    if ( result )
    {
        // Error handling
        // FD Table size gone over
        *retval = result;
        return -1;
    }
    // Set up the file permissions in the fd table
    cp->fdt->fileperms[fd] = flags;
    // We alloced the fd
    result = filp_open( fd , path, flags, mode, retval);

    if ( result )
    {
        // put back fd
        put_fd(cp->fdt, fd);
        *retval = result;
        return -1;
    }

    // if successful then create the oftnode with the vnode pointer, file pos, ref count=0,
    // add this struct into the glob list

    return 0;
}


