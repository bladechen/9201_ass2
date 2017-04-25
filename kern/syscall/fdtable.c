
#include <fdtable.h>
#include <lib.h>
#include <kern/errno.h>
#include <proc.h>
#include <current.h>
#include <file.h>
#include <kern/fcntl.h>
#include <filemacros.h>

static struct proc * getcurproc(void)
{
  return curproc;
}

static void __installfd(oftnode *nodeptr, int fd, int flags)
{
    struct proc *cp = getcurproc();
    cp->fdt->fdesc[fd] = nodeptr;
    cp->fdt->fileperms[fd] = flags;
}

int stdio_init(fdtable* fdt)
{
    // attach Stderr and stdout to fdt 
    int f1 = O_RDONLY;
    int f2 = O_WRONLY;
    int f3 = O_WRONLY;
    mode_t m1 = 0;
    char console[] = "con:";
    int retval;
    int result;
    oftnode *nodeptr;

    // We alloce the fd if possible and return result
    // add entries 0,1 and 2 into the fdtable

    result = filp_open(STDIN, (const_userptr_t) console, f1, m1, &retval, &nodeptr);
    KASSERT(result == 0);
    __installfd(nodeptr, STDIN, f1);
    bitmap_mark(fdt->fdbitmap,STDIN);

    result = filp_open(STDOUT,(const_userptr_t) console,f2, m1, &retval, &nodeptr);
    KASSERT(result == 0);
    __installfd(nodeptr, STDOUT, f2);
    bitmap_mark(fdt->fdbitmap,STDOUT);

    result = filp_open(STDERR,(const_userptr_t) console,f3, m1, &retval, &nodeptr);
    KASSERT(result == 0);
    __installfd(nodeptr, STDERR, f3);
    bitmap_mark(fdt->fdbitmap,STDERR);

    return 0;
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
            // set the bitmap as marked
            bitmap_mark(fdt->fdbitmap, i);
            spinlock_release(&(fdt->fdlock));
            return 0;
        }
        spinlock_release(&(fdt->fdlock));
    }
    // Return error if all the tables are used
    return EMFILE;
}

static void putback_fd(fdtable *fdt, int fd)
{
    if ( fd<0 || fd >= MAXFDTPROCESS )
    {
        kprintf("Invalid fd number to put back");
        return;
    }

    // this lock is not really necessary i think
    spinlock_acquire(&(fdt->fdlock));
    if ( bitmap_isset(fdt->fdbitmap, fd) != 0)
    {
        bitmap_unmark(fdt->fdbitmap,fd);
        fdt->fileperms[fd] = 0; 
    }
    spinlock_release(&(fdt->fdlock));
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
    oftnode *nodeptr;
    // We alloce the fd if possible and return result
    // if successful then create the oftnode with the vnode pointer, file pos, ref count=0,
    result = filp_open(fd , path, flags, mode, retval, &nodeptr);
    
    if ( result )
    {
        // If alloc failed put back fd
        putback_fd(cp->fdt, fd);
        // reset file permissions
        cp->fdt->fileperms[fd] = 0;
        *retval = result;
        return -1;
    }

    __installfd(nodeptr, fd, flags);
    return 0;
}

static int __verifyfd(int fd)
{
    struct proc *cp = getcurproc();
    if(fd<0 || fd >= MAXFDTPROCESS)
    {
        return EBADF;
    }

    if ( bitmap_isset(cp->fdt->fdbitmap,fd) )
    {
        return 0; 
    }
    return EBADF;
}
static int __verifypermission(int fd, enum rwmode mode)
{
    struct proc *cp = getcurproc();
    int perm = cp->fdt->fileperms[fd];
    if(mode == READ)
    {
        if ( perm == O_RDONLY || perm == O_RDWR )
            return 0;
    }
    else if(mode == WRITE)
    {
        if ( perm == O_WRONLY || perm == O_RDWR )
            return 0;
    }
    return EBADF;
}
static oftnode * get_fd_vnode(int fd)
{
    struct proc *cp = getcurproc();
    return cp->fdt->fdesc[fd]; 
}

ssize_t do_sys_write(int fd, const_userptr_t buf, size_t nbytes, int *retval)
{
    (void)buf;
    (void)nbytes;

    int result;
    // check if the fd is valid
    struct proc *cp = getcurproc();

    spinlock_acquire(&(cp->fdt->fdlock));
    result = __verifyfd(fd);
    spinlock_release(&(cp->fdt->fdlock));

    if ( result )
    {
        *retval = result;
        return -1;
    }
    // Check if permission is valid
    spinlock_acquire(&(cp->fdt->fdlock));
    result = __verifypermission(fd, WRITE);
    spinlock_release(&(cp->fdt->fdlock));

    if ( result )
    {
        *retval = result;
        return -1;
    }
    
    spinlock_acquire(&(cp->fdt->fdlock));
    oftnode *node = get_fd_vnode(fd);
    spinlock_release(&(cp->fdt->fdlock));

    if ( node == NULL )
    {
        *retval = EBADF;
        return -1;
    }

    ssize_t written = write_to_file(node, buf, nbytes, retval);
    if ( written )
    {
        *retval = written;
        return -1;
    }

    return 0;
}

