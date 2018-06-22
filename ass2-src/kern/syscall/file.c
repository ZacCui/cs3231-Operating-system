#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>
#include <proc.h>


#define TABLE_MAX OPEN_MAX*2

int fd_check(int fd) {
    if (fd<3 || fd>=OPEN_MAX) return 1;
    if (curproc->fd_table[fd].index==0) return 1;
    if (of_table[curproc->fd_table[fd].index]==NULL) return 1;
    return 0;
}

int std_fd_check(int fd) {
    if (fd<0 || fd>=OPEN_MAX) return 1;
    if (fd!=0 && curproc->fd_table[fd].index==0) return 1;
    if (of_table[curproc->fd_table[fd].index]==NULL) return 1;
    return 0;
}

int of_create(const char* filename, int flags, mode_t mode, struct opened_file** of) {
    // check file open correctly
    struct vnode* new_node;
    int openret = vfs_open((char*)filename, flags, mode, &new_node);
    if (openret)
        return openret;

    // check of malloc ok
    *of = kmalloc(sizeof(struct opened_file));
    if (*of) {
        (*of)->of_refcount = 1;
        (*of)->of_offset = 0;
        (*of)->of_lock = lock_create("of_lock");
        (*of)->of_vnode = new_node;
    } else {
        vfs_close(new_node);
        kfree(new_node);
        return ENOMEM;
    }

    return 0;
}

int file_init(void) {
    of_table = kmalloc(sizeof(struct opened_file*)*TABLE_MAX);
    if (of_table == NULL) 
        return ENOMEM;
    memset(of_table, 0, TABLE_MAX*sizeof(struct opened_file*));
    // connect to console std(in/out/err)
    char c0[] = "con:";
    char c1[] = "con:";
    char c2[] = "con:";
    int r0 = of_create(c0, O_RDONLY, 0644, &of_table[0]);
    int r1 = of_create(c1, O_WRONLY, 0644, &of_table[1]);
    int r2 = of_create(c2, O_WRONLY, 0644, &of_table[2]);
    if (r0) return r0;
    if (r1) return r1;
    if (r2) return r2;
    of_table[0]->of_refcount = 0;
    of_table[1]->of_refcount = 0;
    of_table[2]->of_refcount = 0;
    dup_lock = lock_create("dup_lock");
    if (dup_lock == NULL) return ENOMEM;

    return 0;
}


// return val > 2 when success
// return val < 0 when fail
int sys_open(const char* filename, int flags, mode_t mode, int* err) {
    // check filename
    if (filename == NULL) {
        *err = EFAULT;
        return -1;
    }

    // check flags
    int baseflags = flags & O_ACCMODE;
    if (baseflags == O_ACCMODE) {
        *err = EINVAL;
        return -1;
    }

    int index;

    // check enough of_table
    for (index=0; index<TABLE_MAX; index++)
        if (of_table[index]==0)
            break;
    if (index == TABLE_MAX) {
        *err = ENFILE;
        return -1;
    }

    // of_create fail
    int temp = of_create(filename, flags, mode, &of_table[index]);
    if (temp) {
        *err = temp;
        return -1;
    }

    // set append mode if required
    if (flags & O_APPEND) {
        struct stat info;
        off_t ret;
        ret = VOP_STAT(of_table[index]->of_vnode, &info);
        if (ret) {
            *err = ret;
            return -1;
        }
        of_table[index]->of_offset = info.st_size;
    }

    int fd_index;
    for (fd_index=3; fd_index<OPEN_MAX; fd_index++)
        if (curproc->fd_table[fd_index].index == 0) {
            curproc->fd_table[fd_index].index = index;
            curproc->fd_table[fd_index].mode  = baseflags;
            return fd_index;
        }

    // full fd_table
    *err = EMFILE;
    return -1;
}

// return readed bytes when success
// return val < 0 when fail
ssize_t sys_read(int fd, void *buf, size_t nbytes, int* err) {
	// handle bad reference
	if (fd < 0 || fd >= OPEN_MAX) {
        *err = EBADF;
        return -1;
    }

    // check buffer
    if (buf == NULL) {
        *err = EFAULT;
        return -1;
    }

    // check of_table
	struct file_descriptor * fdp = &(curproc->fd_table[fd]);
    if(of_table[fdp->index] == NULL) {
        *err = EBADF;
        return -1;
    }

	//Check the premission
	if(fdp->mode == O_WRONLY) {
        *err = EBADF;
        return -1;
    }

	struct opened_file * of = of_table[fdp->index];

	//for debug
	KASSERT(of != NULL); 

	void * kbuf = kmalloc(sizeof(void *) * nbytes);
	if(kbuf == NULL) {
        *err = EFAULT;
        return -1;
    }

	struct iovec _iovec;
	struct uio _uio;
	int result;

	_iovec.iov_ubase = (userptr_t) buf;

    //lock offset and start reading
	lock_acquire(of->of_lock);

	uio_kinit(&_iovec, &_uio, kbuf, nbytes, of->of_offset, UIO_READ);

	result = VOP_READ(of->of_vnode, &_uio);

	if (result)
	{
		lock_release(of->of_lock);
		kfree(kbuf);
        *err = result;
		return -1;
	}

    // the length of read
    int len = nbytes - _uio.uio_resid;
    // copy the kernal buffer to userland buffer
    result = copyout(kbuf, buf, len);

	kfree(kbuf);
    if (result && len) {
        lock_release(of->of_lock);
        *err = result;
        return -1;
    }
    // update offset
	of->of_offset += len;
	lock_release(of->of_lock);

	return len;
}

// return wrote bytes when success
// return val < 0 when fail
ssize_t sys_write(int fd, const void *buf, size_t nbytes, int* err) {
	// handle bad reference
	if (fd < 0 || fd >= OPEN_MAX) {
        *err = EBADF;
        return -1;
    }

    // check buffer
    if (buf == NULL) {
        *err = EFAULT;
        return -1;
    }

    // check of_table 
	struct file_descriptor * fdp = &(curproc->fd_table[fd]);

    if(of_table[fdp->index] == NULL) {
        *err = EBADF;
        return -1;
    }

	//Check the premission
	if(fdp->mode == O_RDONLY) {
        *err = EBADF;
        return -1;
    }

	struct opened_file * of = of_table[fdp->index];

	//for debug
	KASSERT(of != NULL); 

	void * kbuf = kmalloc(sizeof(void *) * nbytes);
	if(kbuf == NULL) {
        *err = EFAULT;
        return -1;
    }

	int result;

    //copy the userland buffer to kernal buffer
	result = copyin((const_userptr_t) buf, kbuf, nbytes);
	if (result && nbytes)
	{
		kfree(kbuf);
        *err = result;
		return -1;
	}

	struct iovec _iovec;
	struct uio _uio;
	_iovec.iov_ubase = (userptr_t) buf;

    //lock the offset and start writing
	lock_acquire(of->of_lock);

	uio_kinit(&_iovec, &_uio, kbuf, nbytes, of->of_offset, UIO_WRITE);
	result = VOP_WRITE(of->of_vnode, &_uio);

	if (result)
	{
		lock_release(of->of_lock);
		kfree(kbuf);
        *err = result;
		return -1;
	}

    // length of written
    int len = nbytes - _uio.uio_resid;
    // update offset
	of->of_offset += len;
	lock_release(of->of_lock);
	kfree(kbuf);

	return len;
}

// return offset bytes when success
// return val < 0 when fail
off_t sys_lseek(int fd, uint64_t offset, int whence, int* err) {

    // check fd
    if (std_fd_check(fd)) {
        *err = EBADF;
        return -1;
    }
    if (fd_check(fd)) {
        *err = ESPIPE;
        return -1;
    }

    // check is seekable or not
    if (!VOP_ISSEEKABLE(of_table[curproc->fd_table[fd].index]->of_vnode)) {
        *err = ESPIPE;
        return -1;
    }

    struct stat info;
    off_t ret;
    ret = VOP_STAT(of_table[curproc->fd_table[fd].index]->of_vnode, &info);
    if (ret) {
        *err = ret;
        return -1;
    }

    // lock offset and start lseeking
    off_t len = info.st_size;
    lock_acquire(of_table[curproc->fd_table[fd].index]->of_lock);
    switch (whence) {
        case SEEK_SET:
            ret = offset;
            break;
        case SEEK_CUR:
            ret = of_table[curproc->fd_table[fd].index]->of_offset + offset;
            break;
        case SEEK_END:
            ret = len + offset;
            break;
        default:
            lock_release(of_table[curproc->fd_table[fd].index]->of_lock);
            *err = EINVAL;
            return -1;
    }

    if (ret<0 || ret>len) {
        lock_release(of_table[curproc->fd_table[fd].index]->of_lock);
        *err = EINVAL;
        return -1;
    }

    // update offset
    of_table[curproc->fd_table[fd].index]->of_offset = ret;
    lock_release(of_table[curproc->fd_table[fd].index]->of_lock);
    return ret;
}

// return val < 0 when fail
int sys_close(int fd, int* err) {
    // check fd
    lock_acquire(dup_lock);
    if (fd_check(fd)) {
        *err = EBADF;
        lock_release(dup_lock);
        return -1;
    }

    // reduce the connections by one
    of_table[curproc->fd_table[fd].index]->of_refcount--;

    // no one openning the file
    if (of_table[curproc->fd_table[fd].index]->of_refcount==0) {
        vfs_close(of_table[curproc->fd_table[fd].index]->of_vnode);
        lock_destroy(of_table[curproc->fd_table[fd].index]->of_lock);
        of_table[curproc->fd_table[fd].index]->of_offset = 0;
        of_table[curproc->fd_table[fd].index]->of_vnode = NULL;
        of_table[curproc->fd_table[fd].index] = NULL;
        curproc->fd_table[fd].index = 0;
        curproc->fd_table[fd].mode = 0;
    }

    lock_release(dup_lock);
    return 0;
}

// return 0 when success
// return val < 0 when fail
int sys_dup2(int from, int to, int* err) {

    // check fd
    lock_acquire(dup_lock);
    if (std_fd_check(from) || to<0 || to >= OPEN_MAX) {
        *err = EBADF;
        lock_release(dup_lock);
        return -1;
    }

    // check is the same fd or not
    if (from==to) {
        lock_release(dup_lock);
        return to;
    }
    
    // start dup2
    if (to<3 || (to>2 && curproc->fd_table[to].index==0)) {
        of_table[curproc->fd_table[to].index]->of_refcount--;
        if (of_table[curproc->fd_table[to].index]->of_refcount==0) {
            vfs_close(of_table[curproc->fd_table[to].index]->of_vnode);
            lock_destroy(of_table[curproc->fd_table[to].index]->of_lock);
            of_table[curproc->fd_table[to].index]->of_offset = 0;
            of_table[curproc->fd_table[to].index]->of_vnode = NULL;
            of_table[curproc->fd_table[to].index] = NULL;
        }
    }

    curproc->fd_table[to].index = curproc->fd_table[from].index;
    curproc->fd_table[to].mode = curproc->fd_table[from].mode;
    of_table[curproc->fd_table[to].index]->of_refcount++;
    lock_release(dup_lock);
    return to;
}
