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

//#define OPEN_MAX2 OPEN_MAX 
#define OPEN_MAX2 10 // for debug
#define IS_DEBUG_FILEDESCRIPTOR 0 // 1 to enable show_of_table() and show_fd_table(). 
#define DEBUG_MODE 1 // several printf for err
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
    strcpy(filename_copy, filename);
     
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
        kprintf("\n\n-----------------opening %s: -------------------\n", filename);
    }
    if (result) {
        if (DEBUG_MODE) kprintf("vfs open failed: filename = %s\nflag = %d, mode = %u, error = %d", filename, flags, mode, result);
        return result;
    }
    //kprintf("I am still alive\n");

    /* update of table */
    cur_of_table->fp[of_free_slot] = 0;
    cur_of_table->v_ptr[of_free_slot] = v;
    cur_of_table->availability[of_free_slot] = OCCUPIED;
    cur_of_table->refcount[of_free_slot] = 1;
    cur_of_table->flags[of_free_slot] = flags;
    /* update fd table */
    curproc->fd_tbl->of_ptr[fd_free_slot] = of_free_slot;
    curproc->fd_tbl->availability[fd_free_slot] = OCCUPIED;

    if (IS_DEBUG_FILEDESCRIPTOR) show_of_table(); // debug code: show the status of of_table
    if (IS_DEBUG_FILEDESCRIPTOR) show_fd_table(); // debug code: show the status of fd_table

    return fd_free_slot;   
}


int sys_read(int filehandle, void *buf, size_t size, int *err){
    /*return value*/
    int result = 0;

    /* check availability of filehandle*/
    if (curproc->fd_tbl->availability[filehandle] == FREE){
        if (DEBUG_MODE) kprintf("can't read fd = %d : this fd is free\n", filehandle);
        *err = EBADF;
        return -1;
    }
    

    /* check whether buffer is available */
    if (buf == NULL){
        if (DEBUG_MODE) kprintf("the buffer is not available or the size is not available\n");
        *err = EFAULT;
        return -1;
    }
    /* check availability of open file entry */
    int of_index = curproc->fd_tbl->of_ptr[filehandle];
    if (cur_of_table->availability[of_index] == FREE){
        if (DEBUG_MODE) kprintf("can't read of = %d : this of is free\n", of_index);
        *err = EBADF;
        return -1;
    }

    /* check mode */
    struct stat cur_file_stat;
    /* get the stat of current file first */
    result = VOP_STAT(cur_of_table->v_ptr[of_index],&cur_file_stat);
    if (result){
        *err = result;
        return -1;
    }
    int how = cur_file_stat.st_mode&O_ACCMODE;
    switch(how){
        case O_RDONLY:
        case O_RDWR:
        break;
        default:
        *err = EBADF;
        return -1;
    }

    struct iovec *iov = kmalloc(sizeof(struct iovec));
    struct uio *u = kmalloc(sizeof(struct uio));

    uio_kinit(iov, u, buf, size, cur_of_table->fp[of_index],UIO_READ);
   
    *err = VOP_READ(cur_of_table->v_ptr[of_index],u); 
    /*check VOP_READ SUCCESS*/
    if (*err) {
        if (DEBUG_MODE) kprintf("some error in read(): retval is %d\n",*err);
        return -1;
    }
    
    result = u->uio_offset - cur_of_table->fp[of_index];
    cur_of_table->fp[of_index] = u->uio_offset;
    kfree(iov);
    kfree(u);
    /* should add handler for remain of u.uio_resid here */
    //cur_of_table->fp[of_index] += size;
    return result;
}


int sys_write(int filehandle, const_userptr_t buf, size_t size, int *err){
    /*return value*/
    int result = 0;
    *err = 0;
    
    /* check the file pointer */
    if (curproc->fd_tbl->availability[filehandle] == FREE){
        if (DEBUG_MODE) kprintf("can't read fd = %d : this fd is free\n", filehandle);
        *err = EBADF;
        return -1;
    }

    /* check whether buffer is available */
    if (buf == NULL){
        if (DEBUG_MODE) kprintf("the buffer is not available or the size is not available\n");
        *err = EFAULT;
        return -1;
    }
    // duplicate string
    const char *str = (const char *)buf; // may need to replace this duplication into copyinstr
    char *duplicate_str = kmalloc(sizeof(char) * (size+1)); 
    size_t i = 0;
    for (i = 0; i != size; i++){
        duplicate_str[i] = str[i];
    }// may need to replace this duplication into copyinstr

    int of_index = curproc->fd_tbl->of_ptr[filehandle];
    if (cur_of_table->availability[of_index] == FREE){
        if (DEBUG_MODE) kprintf("can't read of = %d : this of is free\n", of_index);
        *err = EBADF;
        return -1;
    }


    // I don't know why this always fail. May instead to put read/write mode in of_table
    /* get the stat of current file first */
    // struct stat cur_file_stat;
    // result = VOP_STAT(cur_of_table->v_ptr[of_index],&cur_file_stat);
    // if (result){
    //     return result;
    // }

    // int how = cur_file_stat.st_mode&O_ACCMODE;
    // switch(how){
    //     case O_WRONLY:
    //     case O_RDWR:
    //     break;
    //     default:
    //     kprintf("not suporting writing! cur_file_stat.st_mode = 0x%x,  how = %d\n", cur_file_stat.st_mode, how);
    //     return EBADF;
    // }
    int how = cur_of_table->flags[of_index]&O_ACCMODE;
    if (how == O_RDONLY){
        if (DEBUG_MODE) kprintf("not suporting writing! flags = 0x%x,  how = %d\n", cur_of_table->flags[of_index], how);
        *err = EBADF;
        return -1;
    }
    
    
    // initialize uio
    struct iovec *iov = kmalloc(sizeof(struct iovec));
    struct uio *u = kmalloc(sizeof(struct uio)); 
    uio_kinit(iov, u, (void *)duplicate_str, size, cur_of_table->fp[of_index], UIO_WRITE);
    
    *err = VOP_WRITE(cur_of_table->v_ptr[of_index], u);
    if (*err) {
        if (DEBUG_MODE) kprintf("some error in write(): retval is %d\n", *err);
        return -1;
    }

    /* find the write offset */
    result = u->uio_offset - cur_of_table->fp[of_index];

    /* update te offset */
    cur_of_table->fp[of_index] = u->uio_offset;
    kfree(duplicate_str);
    kfree(iov);
    kfree(u);
    //if(filehandle > 2) kprintf("write return size = %d\n", size);
    //cur_of_table->fp[of_index] += size;
    return result;
}

int sys_lseek(int filehandle, off_t *pos, int whence, int *err){
    /* returned value */
    int result = 0;

    /* check fd availablity */
    if ((filehandle < 0) || (filehandle > OPEN_MAX2 + 1)){
        if (DEBUG_MODE) kprintf("unsupported seek object\n");
        *err = ESPIPE;
        return -1;
    }

    /* check filehandle exists or not */
    if (curproc->fd_tbl->availability[filehandle] == FREE){
        if (DEBUG_MODE) kprintf("can't read fd = %d : this fd is free\n", filehandle);
        *err = EBADF;
        return -1;
    }

    int of_index = curproc->fd_tbl->of_ptr[filehandle];
    if (cur_of_table->availability[of_index] == FREE){
        if (DEBUG_MODE) kprintf("can't read of = %d : this of is free\n", of_index);
        *err = EBADF;
        return -1;
    }

    /* check vnode seekablity */
    if (!VOP_ISSEEKABLE(cur_of_table->v_ptr[of_index])){
        *err = ESPIPE;
        return -1;
    }

    struct stat cur_file_stat;
    /* get the stat of current file first */
    *err = VOP_STAT(cur_of_table->v_ptr[of_index],&cur_file_stat);
    if (*err){
        return -1;
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
                *err = EINVAL;
                return -1;
            }
            cur_of_table->fp[of_index] += pos;
            break;

        case SEEK_END:
            /* final fp can't be negative */
            if(cur_file_stat.st_size + pos < 0){
                *err = EINVAL;
                return -1;
            }
            cur_of_table->fp[of_index] = cur_file_stat.st_size + pos;
            break;
    }
    result = cur_of_table->fp[of_index];
    return result;
}


int sys_close(int filehandle){
    /* returned value */
    int result = 0;

    if (IS_DEBUG_FILEDESCRIPTOR) kprintf("\n\n-----------------closing fd %d: -------------------------\n", filehandle);

    /* check availability of fd table */
    if (curproc->fd_tbl->availability[filehandle] == FREE) {
        if (DEBUG_MODE) kprintf("ERROR: fd %d is free! can't close\n", filehandle);
        return EBADF; /* return bad file number*/
    }

    int of_index = curproc->fd_tbl->of_ptr[filehandle];
    if (cur_of_table->availability[of_index] == FREE){
        if (DEBUG_MODE) kprintf("ERROR: of %d is free! can't close\n", of_index);
        return EBADF; /* return bad file number */
    }

    if (push_fd_empty_free_slot(curproc->fd_tbl, filehandle) == -1) {
        return EMFILE; /* return too many open files */
    }
    curproc->fd_tbl->availability[filehandle] = FREE;

    /* check the reference count of curr entry */
    if (cur_of_table->refcount[of_index] > 1){
        cur_of_table->refcount[of_index] --;
    }
    else{
        if (push_of_empty_free_slot(cur_of_table, of_index) == -1){
            return ENFILE; /* return too many global open files */
        }
        cur_of_table->refcount[of_index]--;
        cur_of_table->availability[of_index] = FREE; /* set entry free */
        vfs_close(cur_of_table->v_ptr[of_index]); /* free the vnode */
    }

    if (IS_DEBUG_FILEDESCRIPTOR) show_of_table(); // debug code: show the status of of_table
    if (IS_DEBUG_FILEDESCRIPTOR) show_fd_table(); // debug code: show the status of fd_table

    return result;
}


int sys_dup2(int filehandle, int newhandle){
    /* returned value */
    int result = 0;

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
        if (cur_of_table->refcount[of_index] > 1){
            cur_of_table->refcount[of_index]--;
        }
        else{
            if (push_of_empty_free_slot(cur_of_table, of_index) == -1){
                return ENFILE; /* return too many global open files */
            }
            cur_of_table->availability[of_index] = FREE; /* set entry free */
            vfs_close(cur_of_table->v_ptr[of_index]); /* free the vnode */
        }
        
        /* make the second fd point to first one */
        curproc->fd_tbl->of_ptr[newhandle] = curproc->fd_tbl->of_ptr[filehandle];
        /* find the entry in of_table of first fd */
        of_index = curproc->fd_tbl->of_ptr[filehandle];
        cur_of_table->refcount[of_index]++;
    }
    else{
        curproc->fd_tbl->of_ptr[newhandle] = curproc->fd_tbl->of_ptr[filehandle];
        curproc->fd_tbl->availability[newhandle] = OCCUPIED;
        cur_of_table->refcount[filehandle]++;
    }

    return result;
}


struct fd_table * create_fd_table(void){
    struct fd_table * fd = kmalloc(sizeof(struct fd_table));
    KASSERT(fd != NULL);
    fd->of_ptr = kmalloc(sizeof(int) * OPEN_MAX2);
    fd->free_slots = kmalloc(sizeof(int) * OPEN_MAX2);
    fd->availability= kmalloc(sizeof(int8_t) * OPEN_MAX2);
    fd->size = 0;
    fd->front = 0;
    fd->end = 0;

    int i;
    for (i = 0; i < OPEN_MAX2; i++){
        queue_push(fd->free_slots, &(fd->end), &(fd->size), i);
        fd->availability[i] = FREE;
    }
    return fd;
}

struct of_table * create_of_table(void){
    struct of_table *of = kmalloc(sizeof(struct of_table));
    KASSERT(of != NULL);
    of->fp = kmalloc(sizeof(off_t) * OPEN_MAX2);
    of->v_ptr = kmalloc(sizeof(struct vnode) * OPEN_MAX2);
    of->flags = kmalloc(sizeof(mode_t) * OPEN_MAX2);
    of->free_slots = kmalloc(sizeof(int) * OPEN_MAX2);
    of->availability= kmalloc(sizeof(int8_t) * OPEN_MAX2);
    of->size = 0;
    of->front = 0;
    of->end = 0;
    of->refcount = kmalloc(sizeof(int) * OPEN_MAX2);

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
        if (DEBUG_MODE) kprintf("of table is full\n");
        return -1;
    }
    queue_pop(table->free_slots, &(table->front), &(table->size));
    int free_slot = table->free_slots[of_free_index];
    return free_slot;
}

int get_fd_table_free_slot(struct fd_table *table){
    int of_free_index = queue_front(table->free_slots, table->front, table->size);
    if (of_free_index == -1) {
        if (DEBUG_MODE) kprintf("of table is full\n");
        return -1;
    }
    queue_pop(table->free_slots, &(table->front), &(table->size));
    int free_slot = table->free_slots[of_free_index];
    return free_slot;
}


int push_of_empty_free_slot(struct of_table *table, int element){
    int retval = queue_push(table->free_slots, &(table->end), &(table->size), element);
    if (retval == -1 && DEBUG_MODE) kprintf("of table free slot is full, can't push");
    return retval;
}
int push_fd_empty_free_slot(struct fd_table *table, int element){
    int retval = queue_push(table->free_slots, &(table->end), &(table->size), element);
    if (retval == -1 && DEBUG_MODE) kprintf("fd table free slot is full, can't push");
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
        else    kprintf("fp = %lld, vnode = %x : %d, flags = 0x%x\n", table->fp[i], (unsigned int)table->v_ptr[i], 
            table->v_ptr[i]->vn_refcount, table->flags[i]);
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
        else    kprintf("ofptr = %d\n", table->of_ptr[i]);
    }
}
