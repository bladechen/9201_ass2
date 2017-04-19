#ifndef _FILE_H_
#define _FILE_H_

#include <spinlock.h>
#include <synch.h>
#include <stat.h>
#include <list.h>
#include "vnode.h"


struct vnode;
struct fs;

/*
 * kernel file, which is pointed by file descripter
 * linked in intrusive list
 *
 */

struct files_table;
struct file
{
    struct list_head link_obj; /* link unit of intrusive list*/

    volatile int ref_count;
    // struct spinlock f_lock;

    struct vnode* v_ptr;

    int f_flags;
    off_t f_pos; // the current seek position of the file
    struct lock*  file_op_lock;
    struct stat f_stat;

    struct files_table* owner;


};

struct files_table
{
    struct spinlock files_table_lock;
    // struct lock*
    struct list* list_obj;

};

void inc_ref_file(struct file* f)  ;
int close_kern_file(struct file* fs, struct spinlock* fs_lock);
int do_flip_open(struct file ** fp, int dfd, char* filename, int flags, mode_t mode);
off_t kern_file_seek(struct file* f,  off_t pos, int whence);
ssize_t kern_file_read(struct file* f, char* buf, size_t buf_size, size_t* read_len);

int kern_file_write(struct file* f, const void * buf, size_t buf_size, size_t * read_len);


void init_kern_file_table(void);
void destroy_kern_file_table(void);


#endif
