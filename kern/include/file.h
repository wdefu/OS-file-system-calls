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
#include <synch.h>


/*
 * Put your function declarations and data types here ...
 */


/*
 * struct used to store opened vnode and corresponding file pointers
*/
struct fd_table {
    int *of_ptr; /* map to the index in of_table */
    int *free_slots; /* queue of free ofptr */
    int8_t *availability; /* FREE or OCCUPIED: check if ofptr[i] is free or not */
    int size; /* number of free slots */
    int front; /* front of free slot queue */
    int end; /* end of free slot queue */
};


struct of_table{
    struct vnode **v_ptr;
    off_t *fp; /* file pointer. I don't know how this works */
    int8_t *availability; /* FREE or OCCUPIED: check if v_ptr[i] and fp[i] is free or not */
    mode_t *flags; 
    int *free_slots; /* queue of free v_ptr */
    int size; /* size of current file_des_table */
    int front; /* front of free slot queue */
    int end; /* end of free slot queue */
    int *refcount; /* used for dup2 count */
    struct lock **op_lock; /* lock for every op */
};



struct of_table * cur_of_table;

struct fd_table * create_fd_table(void);
struct of_table * create_of_table(void);


int sys_open(const char *filename, int flags, mode_t mode);
int sys_read(int filehandle, void *buf, size_t size, int *err);
int sys_write(int filehandle, const_userptr_t buf, size_t size, int *err);
off_t sys_lseek(int filehandle, off_t pos, int whence, int *err);
int sys_close(int filehandle);
int sys_dup2(int filehandle, int newhandle);

void show_of_table(void);
void show_fd_table(void);



#endif /* _FILE_H_ */






