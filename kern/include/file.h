/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

#define FREE 0
#define OCCUPIED 1
/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>
#include <vnode.h>


/*
 * Put your function declarations and data types here ...
 */


/*
 * struct used to store opened vnode and corresponding file pointers
*/
struct fd_table {
    int *of_ptr; /* map to the index in of_table */
    int8_t *availability; /* FREE or OCCUPIED: check if ofptr[i] is free or not */
};


struct of_table{
    struct vnode **v_ptr;
    off_t *fp; /* file pointer. I don't know how this works */
    int8_t *availability; /* FREE or OCCUPIED: check if v_ptr[i] and fp[i] is free or not */
    int size; /* size of current file_des_table */
    int *mode;
    int *refcount; /* used for dup2 count */
};


struct of_table * cur_of_table;

struct fd_table * create_fd_table(void);
struct of_table * create_of_table(void);


int sys_open(userptr_t filename, int flags, mode_t mode,int32_t *final_handle);
int sys_read(int filehandle, void *buf, size_t size);
int sys_write(int filehandle, const_userptr_t buf, size_t size);
int sys_lseek(int filehandle, off_t pos, int code);
int sys_close(int filehandle);
int sys_dup2(int filehandle, int newhandle);

void show_of_table(void);
void show_fd_table(void);



#endif /* _FILE_H_ */










































