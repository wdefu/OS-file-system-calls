/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>


/*
 * Put your function declarations and data types here ...
 */


int sys_open(const char *filename, int flags, mode_t mode);
ssize_t sys_read(int filehandle, void *buf, size_t size);
ssize_t sys_write(int filehandle, const void *buf, size_t size);
off_t sys_lseek(int filehandle, off_t pos, int code);
int sys_close(int filehandle);
int sys_dup2(int filehandle, int newhandle);

struct vnode {
	int vn_refcount;                /* Reference count */
	struct spinlock vn_countlock;   /* Lock for vn_refcount */

	struct fs *vn_fs;               /* Filesystem vnode belongs to */

	void *vn_data;                  /* Filesystem-specific data */

	const struct vnode_ops *vn_ops; /* Functions on this vnode */
};

/*
 * struct used to store opened vnode and corresponding file pointers
*/
struct op_file_entry {
    vnode *single_vnode;
    off_t fp;
}

int create_op_table(*op_file_table);

struct file_des_table{
    char *p_name;
    int fd_curr_proc;
    struct op_file_entry *curr_fd_entry;
}


#endif /* _FILE_H_ */










































