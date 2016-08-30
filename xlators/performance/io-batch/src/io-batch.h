//
// Created by syd on 16-8-26.
//

#ifndef _IO_BATCH_H
#define _IO_BATCH_H


int
iob_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata);


#endif //GLUSTERFS_IO_BATCH_H
