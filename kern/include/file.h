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

void show_of_table(void);
void show_fd_table(void);


/*
 * struct used to store opened vnode and corresponding file pointers
*/
struct fd_table {
    int *ofptr; //map to the index in of_table
    int *free_slots; // queue of free ofptr
    int8_t *availability; // FREE or OCCUPIED: check if ofptr[i] is free or not
    int size; // number of free slots
    int front; // front of free slot queue
    int end; // end of free slot queue

}

int create_op_table(*op_file_table);

struct of_table{
   struct vnode **v_ptr;
    off_t *fp; // file pointer. I don't know how this works
    int8_t *availability; // FREE or OCCUPIED: check if v_ptr[i] and fp[i] is free or not
    int *free_slots; // queue of free v_ptr
    int size; //size of current file_des_table
    int front; //front of free slot queue
    int end; //end of free slot queue
    int refcount; //used for dup2 count
}


struct of_table * cur_of_table;
/*
 * Put your function declarations and data types here ...
 */
struct fd_table * create_fd_table(void);
struct of_table * create_of_table(void);

void show_of_table(void);
void show_fd_table(void);


#endif /* _FILE_H_ */










































