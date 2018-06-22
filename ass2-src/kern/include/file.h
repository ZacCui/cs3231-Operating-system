/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>
#include <vnode.h>
#include <synch.h>


/*
 * Put your function declarations and data types here ...
 */

struct file_descriptor {
    int             index;
    int             mode;
};

struct opened_file {
    struct vnode*   of_vnode;
    off_t           of_offset;
    int             of_refcount;
    struct lock*    of_lock;
};

struct opened_file** of_table;
struct lock* dup_lock;

int fd_check(int fd);
int std_fd_check(int fd);
int of_create(const char* filename, int flags, mode_t mode, struct opened_file** of);
int file_init(void);

int sys_open(const char* filename, int flags, mode_t mode, int* err);
ssize_t sys_read(int fd, void *buf, size_t nbytes, int* err);
ssize_t sys_write(int fd, const void *buf, size_t nbytes, int* err);
off_t sys_lseek(int fd, uint64_t offset, int mode, int* err);
int sys_close(int fd, int* err);
int sys_dup2(int from, int to, int* err);



#endif /* _FILE_H_ */
