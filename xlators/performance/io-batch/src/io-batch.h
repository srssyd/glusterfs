//
// Created by syd on 16-8-26.
//

#ifndef _IO_BATCH_H
#define _IO_BATCH_H

typedef struct iob_conf{
    uint64_t batch_size;
    //The maximal total size of buffer that can be remained in memory.
    uint64_t total_batch_size;
    //Flushed after certain time out in milliseconds;
    uint32_t flush_time_out;
}iob_conf_t;

typedef struct iob_buffer{
    dict_t *buffer_dic;
    uint64_t total_buffered_size;
    gf_lock_t lock;

    iob_conf_t conf;

}iob_buffer_t;

typedef struct iob_buffer_inode{
    struct rb_table *vectors;
    uint64_t buffered_size;
    pthread_mutex_t mutex;
}iob_buffer_inode_t;


typedef enum iob_read_status{
    SIMPLE,
    CACHED,
    PARTIAL,
    PREFETCH
}iob_read_status_t;

typedef enum iob_interval_status{
    WRITTEN,
    BUFFERED
}iob_interval_status_t;

typedef struct iob_interval{
    struct iovec *iov;
    struct iobref * iobref;
    uint32_t     count;
    off_t        off;
    size_t     length;
    iob_interval_status_t status;
}iob_interval_t;

typedef struct iob_shared{
    int ref;
    gf_lock_t lock;
    int op_ret;
    int op_errno;

}iob_shared_t;

typedef struct iob_write_frame{
    //The pointer is used to free the memory used by the buffer.
    struct iobref * iobref;
    //To restore back to the original call.
    call_frame_t * frame;
    //Things shared by frames, used to combine many callbacks.
    iob_shared_t * shared;

    fd_t        * fd;
    dict_t     * xdata;

    //To distinguish flush and normal write.
    glusterfs_fop_t fop;
}iob_write_frame_t;


typedef struct iob_read_frame{
    iob_read_status_t   status;
    pthread_mutex_t     *lock;

    call_frame_t        *frame;
    iob_buffer_inode_t  *inode_buffer;
    size_t              size;
    off_t               offset;
}iob_read_frame_t;


int
iob_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata);

int iob_readv_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                      int32_t op_ret, int32_t op_errno, struct iovec * vector,
                      int32_t count, struct iatt * stbuf,
                      struct iobref * iobref, dict_t * xdata);

#endif //GLUSTERFS_IO_BATCH_H
