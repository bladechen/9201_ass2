/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>
#include <list.h>

struct oftable {

    struct vnode *vp;
    off_t filepos;
    int refcount;
}

struct {

}

#endif /* _FILE_H_ */
