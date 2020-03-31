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

#define OPEN_MAX2 10 // for debug
#define IS_DEBUG_FILEDESCRIPTOR 0 // 1 to enable show_of_table() and show_fd_table(). 
/*
 * Add your file-related functions here ...
 */
 
int queue_pop(int *queue, int *front, int *size); // return 0 if non-empty, -1 else
int queue_front(int *queue, int front, int size); // return queue[front] if non-empty, -1 else
int queue_push(int *queue, int *end, int *size, int element); // return 0 if not full, -1 else
int get_of_table_free_slot(struct of_table *table);
int get_fd_table_free_slot(struct fd_table *table);
int push_of_empty_free_slot(struct of_table *table, int element);
int push_fd_empty_free_slot(struct fd_table *table, int element);



int sys_open(const char *filename, int flags, mode_t mode){
    /* if filename is null return error */
    if (filename == NULL){
        return ENOENT; //return not a dir error
    }

    struct vnode *v;
    /* copy filename */
    char *filename_copy = kmalloc((strlen(filename) + 5) * sizeof(char));
    strcopy(filename_copy,filename_copy);
     
    /* check whether current of is full of not */
    int of_free_slot = get_of_table_free_slot(cur_of_table);// get free slot from the queue
    if (of_free_slot == -1){
        return ENFILE; // of_table is full. should return too many files error
    }

    /* check whether current fd is full of not */
    int fd_free_slot = get_fd_table_free_slot(curproc->fd_tbl); // get free slot from the queue
    if (fd_free_slot == -1){
        return EMFILE; // fd_table is full
    };
    
    /*open the vnode*/
    int result = vfs_open(filename_copy,flags,mode,&v);
    kfree(filename_copy);
    if (IS_DEBUG_FILEDESCRIPTOR){
        kprintf("\n\n-----------------opening %s: -------------------\n", filename_copy);
    }
    if (result) {
        kprintf("vfs open failed: filename = %s\nflag = %d, mode = %u, error = %d", filename, flags, mode, result);
        return result;
    }

    /* update of table */
    cur_of_table->fp[of_free_slot] = 0;
    cur_of_table->v_ptr[of_free_slot] = v;
    cur_of_table->availability[of_free_slot] = OCCUPIED;

    /* update fd table */
    curproc->fd_tbl->ofptr[fd_free_slot] = of_free_slot;
    curproc->fd_tbl->availability[fd_free_slot] = OCCUPIED;

    if (IS_DEBUG_FILEDESCRIPTOR) show_of_table(); // debug code: show the status of of_table
    if (IS_DEBUG_FILEDESCRIPTOR) show_fd_table(); // debug code: show the status of fd_table

    return fd_free_slot;   
}


int sys_read(int filehandle, void *buf, size_t size){
    /*return value*/
    int result = 0;

    /* check availability of filehandle*/
    if (curproc->fd_tbl->availability[filehandle] == FREE){
        kprintf("can't read fd = %d : this fd is free\n", filehandle);
        return EBADF;
    }
    
    /* check whether buffer is available */
    if (buf == NULL || size < 0){
        kprintf("the buffer is not available or the size is not available\n");
        return EFAULT;
    }
    /* check availability of open file entry */
    int of_index = curproc->fd_tbl->ofptr[filehandle];
    if (cur_of_table->availability[of_index] == FREE){
        kprintf("can't read of = %d : this of is free\n", of_index);
        return EBADF;
    }

    struct iovec *iov = kmalloc(sizeof(struct iovec));
    struct uio *u = kmalloc(sizeof(struct uio));

    uio_kinit(iov, u, buf, size, cur_of_table->fp[of_index],UIO_READ);
   
    int err = VOP_READ(cur_of_table->v_ptr[of_index],u);
    kfree(iov);
    kfree(u);
    if (err) {
        kprintf("some error in read(): retval is %d\n",err);
        return err;
    }
    
    /* should add handler for remain of u.uio_resid here */
    cur_of_table->fp[of_index] += size;
    return result;
}


int sys_write(int filehandle, const_userptr_t buf, size_t size){
    /*return value*/
    int result = 0;
    
    /* check the file pointer */
    if (curproc->fd_tbl->availability[filehandle] == FREE){
        kprintf("can't read fd = %d : this fd is free\n", filehandle);
        return -1;
    }

    /* check whether buffer is available */
    if (buf == NULL || size < 0){
        kprint("the buffer is not available or the size is not available\n");
        return EFAULT;
    }
    // duplicate string
    const char *str = (const char *)buf; // may need to replace this duplication into copyinstr
    char *duplicate_str = kmalloc(sizeof(char) * (size+1)); 
    size_t i = 0;
    for (i = 0; i != size; i++){
        duplicate_str[i] = str[i];
    }// may need to replace this duplication into copyinstr

    int of_index = curproc->fd_tbl->ofptr[filehandle];
    if (cur_of_table->availability[of_index] == FREE){
        kprintf("can't read of = %d : this of is free\n", of_index);
        return -1;
    }
    // initialize uio
    struct iovec *iov = kmalloc(sizeof(struct iovec));
    struct uio *u = kmalloc(sizeof(struct uio)); 
    uio_kinit(iov, u, (void *)duplicate_str, size, cur_of_table->fp[of_index], UIO_WRITE);
    
    int err = VOP_WRITE(cur_of_table->v_ptr[of_index], u);
    kfree(duplicate_str);
    kfree(iov);
    kfree(u);
    if (err) {
        kprintf("some error in write(): retval is %d\n", err);
        return err;
    }
    if(filehandle > 2) kprintf("write return size = %d\n", size);
    cur_of_table->fp[of_index] += size;
    return result;
}

int sys_lseek(int filehandle, off_t pos, int whence){
    /* returned value */
    int result = 0;

    /* check fd availablity */
    if (filehandle < 0 || fd >OPEN_MAX2 + 1){
        kprint("unsupported seek object\n");
        return ESPIPE;
    }

    /* check filehandle exists or not */
    if (curproc->fd_tbl->availability[filehandle] == FREE){
        kprintf("can't read fd = %d : this fd is free\n", filehandle);
        return EBADF;
    }

    int of_index = curproc->fd_tbl->ofptr[filehandle];
    if (cur_of_table->availability[of_index] == FREE){
        kprintf("can't read of = %d : this of is free\n", of_index);
        return EBADF;
    }

    /* check vnode seekablity */
    if (!VOP_ISSEEKABLE(cur_of_table->v_ptr[of_index])){
        return ESPIPE;
    }

    struct stat cur_file_stat;
    /* get the stat of current file first */
    result = VOP_STAT(cur_of_table->[v_ptr[of_index]],&cur_file_stat);
    if (result){
        return result;
    }
    
    /* begin to condition */
    switch(whence){

        case SEEK_SET:
            /* pos can't be negative */
            if(pos < 0){
                return EINVAL;
            }
            cur_of_table->fp[of_index] = pos;
            break;

        case SEEK_CUR:
            /* final fp can't be negative */
            if(pos + cur_of_table->fp[of_index] < 0){
                return EINVAL;
            }
            cur_of_table->fp[of_index] += pos;
            break;

        case SEEK_END:
            /* final fp can't be negative */
            if(cur_file_stat.st_size + pos < 0){
                return EINVAL;
            }
            cur_of_table->fp[of_index] = cur_file_stat.st_size + pos;
            break
    }
    
    return result;
}


int sys_close(int filehandle){
    /* returned value */
    int result = 0;

    if (IS_DEBUG_FILEDESCRIPTOR) kprintf("\n\n-----------------closing fd %d: -------------------------\n", filehandle);

    /* check availability of fd table */
    if (curproc->fd_tbl->availability[filehandle] == FREE) {
        kprintf("ERROR: fd %d is free! can't close\n", filehandle);
        return EBADF; /* return bad file number*/
    }

    int of_index = curproc->fd_tbl->ofptr[filehandle];
    if (cur_of_table->availability[if_index] == FREE){
        kprintf("ERROR: of %d is free! can't close\n", of_index);
        return EBADF; /* return bad file number */
    }

    if (push_fd_empty_free_slot(curproc->fd_tbl, filehandle) == -1) {
        return EMFILE; /* return too many open files */
    }
    curproc->fd_tbl->availability[filehandle] = FREE;

    /* check the reference count of curr entry */
    if (cur_of_table[of_index]->refcount > 1){
        cur_of_table[of_index]->refcount--;
    }
    else{
        if (push_of_empty_free_slot(cur_of_table, of_index) == -1){
            return ENFILE; /* return too many global open files */
        }
        cur_of_table->availability[of_index] = FREE; /* set entry free */
        vfs_close(&cur_of_table[of_index]->vptr); /* free the vnode */
    }

    if (IS_DEBUG_FILEDESCRIPTOR) show_of_table(); // debug code: show the status of of_table
    if (IS_DEBUG_FILEDESCRIPTOR) show_fd_table(); // debug code: show the status of fd_table

    return result;
}


int sys_dup2(int filehandle, int newhandle){
    /* returned value */
    int result = 0;
    int of_index;

    /* check filehandles are large than 0 and filehandle exists*/
    if (filehandle < 0 || newhandle <0 || curproc->fd_tbl->availability[filehandle] == FREE){
        return EBADF;
    }

    /* if two handles are equal then has no effect */
    if (filehandle == newhandle){
        return 0;
    }

    /* if newhandle point to some file we need to close it*/
    if (curproc->fd_tbl->availability[newhandle]){
        /* close the current vnode */
        int of_index = curproc->fd_tbl->of_ptr[newhandle];
        if (cur_of_table[of_index]->refcount > 1){
            cur_of_table[of_index]->refcount--；
        }
        else{
            if (push_of_empty_free_slot(cur_of_table, of_index) == -1){
                return ENFILE; /* return too many global open files */
            }
            cur_of_table->availability[of_index] = FREE; /* set entry free */
            vfs_close(&cur_of_table[of_index]->vptr); /* free the vnode */
        }
        
        /* make the second fd point to first one */
        curproc->fd_tbl->ofptr[newhandle] = curproc->fd_tbl->of_ptr[filehandle];
        /* find the entry in of_table of first fd */
        int of_index = curproc->fd_tbl->of_ptr[filehandle];
        cur_of_table[of_index]->refcount++;
    }

    return result;
}


struct of_table * create_of_table(void){
    struct of_table *of = kmalloc(sizeof(struct of_table));
    KASSERT(of != NULL);
    of->fp = kmalloc(sizeof(off_t) * OPEN_MAX2);
    of->v_ptr = kmalloc(sizeof(struct vnode) * OPEN_MAX2);
    of->free_slots = kmalloc(sizeof(int) * OPEN_MAX2);
    of->availability= kmalloc(sizeof(int8_t) * OPEN_MAX2);
    of->size = 0;
    of->front = 0;
    of->end = 0;

    int i;
    for (i = 0; i < OPEN_MAX2; i++){
        queue_push(of->free_slots, &(of->end), &(of->size), i);
        of->availability[i] = FREE;
    }
    return of;
}






// queue operations
int queue_pop(int *queue, int *front, int *size){
    (void) queue;
    if (*size > 0){
        if (++*front >= OPEN_MAX2) *front = 0; // circular increament
        --*size;
        return 0;
    }
    else return -1;
}

int queue_front(int *queue, int front, int size){
    return (size > 0) ? queue[front] : -1;
}

int queue_push(int *queue, int *end, int *size, int element){
    if (*size < OPEN_MAX2){
        queue[*end] = element;
        if (++*end >= OPEN_MAX2) *end = 0; // circular increament
        ++*size;
        return 0;
    }
    else return -1;
} 


int get_of_table_free_slot(struct of_table *table){
    int of_free_index = queue_front(table->free_slots, table->front, table->size);
    if (of_free_index == -1) {
        kprintf("of table is full\n");
        return -1;
    }
    queue_pop(table->free_slots, &(table->front), &(table->size));
    int free_slot = table->free_slots[of_free_index];
    return free_slot;
}

int get_fd_table_free_slot(struct fd_table *table){
    int of_free_index = queue_front(table->free_slots, table->front, table->size);
    if (of_free_index == -1) {
        kprintf("of table is full\n");
        return -1;
    }
    queue_pop(table->free_slots, &(table->front), &(table->size));
    int free_slot = table->free_slots[of_free_index];
    return free_slot;
}


int push_of_empty_free_slot(struct of_table *table, int element){
    int retval = queue_push(table->free_slots, &(table->end), &(table->size), element);
    if (retval == -1) kprintf("of table free slot is full, can't push");
    return retval;
}
int push_fd_empty_free_slot(struct fd_table *table, int element){
    int retval = queue_push(table->free_slots, &(table->end), &(table->size), element);
    if (retval == -1) kprintf("fd table free slot is full, can't push");
    return retval;
}


// for debugging: show of table and show fd table


void show_of_table(void){
    struct of_table *table = cur_of_table;
    kprintf("\nShowing open file table:\n");
    kprintf("free slot queue front index = %d, end index = %d, size = %d\n", table->front, table->end, table->size);

    int i;
    for (i = 0; i < OPEN_MAX2; i++){
        kprintf("position %d: ", i);
        if (table->availability[i] == FREE) kprintf("free\n");
        else    kprintf("fp = %lld, vnode = %x : %d\n", table->fp[i], (unsigned int)table->v_ptr[i], table->v_ptr[i]->vn_refcount);
    }
}
void show_fd_table(void){
    struct fd_table *table = curproc->fd_tbl;
    kprintf("\nShowing file descriptor table:\n");
    kprintf("free slot queue front index = %d, end index = %d, size = %d\n", table->front, table->end, table->size);

    int i;
    for (i = 0; i < OPEN_MAX2; i++){
        kprintf("position %d: ", i);
        if (table->availability[i] == FREE) kprintf("free\n");
        else    kprintf("ofptr = %d\n", table->ofptr[i]);
    }
}
