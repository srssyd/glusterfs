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
    gf_lock_t lock;
}iob_buffer_inode_t;

typedef struct iob_interval{
    struct iovec *iov;
    struct iobref * iobref;
    uint32_t     count;
    off_t        off;
    int     length;
}iob_interval_t;

typedef struct iob_shared{
    int ref;
    gf_lock_t mutex;
    int op_ret;
    int op_errno;

}iob_shared_t;

typedef struct iob_frame{
    //The pointer is used to free the memory used by the buffer.
    struct iobref * iobref;
    //To restore back to the original call.
    call_frame_t * frame;
    //Ref is shared by many frames.
    iob_shared_t * shared;
    fd_t        * fd;
    dict_t     * xdata;

    glusterfs_fop_t fop;
}iob_frame_t;


int
iob_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata);


#endif //GLUSTERFS_IO_BATCH_H
