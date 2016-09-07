//
// Created by syd on 16-8-26.
//

#include <defaults.h>
#include "xlator.h"
#include "io-batch-messages.h"
#include "contrib/rbtree/rb.h"
#include "io-batch.h"
#include "iob-mem-types.h"


int
iob_buffer_vector_comparator(void *entry1, void *entry2, void *param) {
    struct iob_interval *vec1 = entry1;
    struct iob_interval *vec2 = entry2;

    assert(vec1 != NULL);
    assert(vec2 != NULL);
    //No two vector should have same offset for a single inode.
    if (vec1->off == vec2->off)
        return 0;
    return vec1->off < vec2->off ? -1 : 1;

}

int32_t
init(xlator_t *this) {
    iob_conf_t conf;
    iob_buffer_t *buffer = NULL;

    buffer = GF_CALLOC(1, sizeof(iob_buffer_t), gf_iob_mt_buffers);

    conf.batch_size = 128 * 1u << 20;
    conf.total_batch_size = 1024 * 1u << 20;
    conf.flush_time_out = 300;

    buffer->conf = conf;
    buffer->total_buffered_size = 0;
    buffer->buffer_dic = dict_new();
    LOCK_INIT(&buffer->lock);

    this->private = buffer;
}

/* Search |tree| for an item matching |item|, and return it if found.
   Otherwise return the maximum item which is less than item. */
void *
rb_find_max(const struct rb_table *tree, const void *item) {
    const struct rb_node *p;
    void *result = NULL;
    assert (tree != NULL && item != NULL);

    for (p = tree->rb_root; p != NULL;) {
        int cmp = tree->rb_compare(item, p->rb_data, tree->rb_param);

        if (cmp < 0)
            p = p->rb_link[0];
        else if (cmp > 0)
            p = p->rb_link[1], result = p->rb_data;
        else /* |cmp == 0| */
            return p->rb_data;
    }

    return result;
}


void
fini(xlator_t *this) {


    out:
    return;
}

//Not thread safe.
static int iob_flush_buffer(call_frame_t *frame, xlator_t *this, fd_t *fd, iob_buffer_inode_t *inode_buffer,glusterfs_fop_t fop) {
    struct rb_table *vectors = inode_buffer->vectors;
    struct rb_traverser begin_traveser, end_traverser;
    iob_interval_t *interval_begin;
    iob_interval_t *interval_end;
    call_frame_t *bg_frame;
    off_t pre_off, begin_off;
    size_t pre_size;
    uint32_t vector_cnt;
    int32_t ret;
    int i, j, cnt;
    iob_frame_t *iob_frame;
    iob_shared_t *shared = GF_CALLOC(1, sizeof(iob_shared_t), gf_iob_mt_shared);
    LOCK_INIT(&shared->mutex);
    shared->ref = 0;
    shared->op_ret = 0;
    shared->op_errno = 0;
    int total = 0;

    interval_begin = rb_t_first(&begin_traveser, vectors);
    end_traverser = begin_traveser;



    while (interval_begin) {
        pre_off = interval_begin->off;
        begin_off = pre_off;
        pre_size = interval_begin->length;
        vector_cnt = interval_begin->count;
        interval_end = rb_t_next(&end_traverser);
        while (interval_end && interval_end->off == pre_off + pre_size) {
            pre_off = interval_end->off;
            pre_size = interval_end->length;
            vector_cnt += interval_end->count;
            interval_end = rb_t_next(&end_traverser);
        }

        struct iovec *vector_batched = GF_CALLOC(vector_cnt, sizeof(struct iovec), gf_common_mt_iovec);
        struct iobref *iobref = iobref_new();

        iob_interval_t **intervals = malloc(sizeof(iob_interval_t *) * vector_cnt);

        i = 0, cnt = 0;
        while (interval_begin != interval_end) {
            for (j = 0; j < interval_begin->count; j++) {
                vector_batched[i++] = interval_begin->iov[j];
            }

            ret = iobref_merge(iobref, interval_begin->iobref);
            iobref_unref(interval_begin->iobref);

            assert(ret >= 0);
            intervals[cnt++] = interval_begin;
            interval_begin = rb_t_next(&begin_traveser);
        }


        for (i = 0; i < cnt; i++) {
            rb_delete(vectors, intervals[i]);
            GF_FREE(intervals[i]);
        }


        bg_frame = copy_frame(frame);
        assert(bg_frame != NULL);

        iob_frame = GF_CALLOC(1, sizeof(iob_frame_t), gf_iob_mt_frame);
        iob_frame->iobref = iobref;
        iob_frame->frame = frame;
        iob_frame->fd = fd;
        iob_frame->shared = shared;
        iob_frame->fop = fop;
        iob_frame->xdata = NULL;

        LOCK(&shared->mutex);
        shared->ref = shared->ref + 1;
        UNLOCK(&shared->mutex);

        total++;
        //TODO: stack wind after the ref should be at the right position.
        STACK_WIND_COOKIE (bg_frame, iob_writev_cbk, iob_frame,
                           FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev,
                           fd, vector_batched, vector_cnt, begin_off, 0, iobref, NULL);

        end_traverser = begin_traveser;
    }

    assert(rb_count(vectors) == 0);
    inode_buffer->buffered_size = 0;

    return total;
}

//TODO: Handle different flag.
//TODO: Save the xdata.
static size_t
insert_vector(call_frame_t *frame, xlator_t *this, fd_t *fd, iob_buffer_inode_t *inode_buffer, off_t offset,
              struct iovec *vector, uint32_t count, struct iobref *iobref) {
    if (count <= 0)
        return 0;
    struct rb_table *vectors = inode_buffer->vectors;
    iob_interval_t *interval = GF_CALLOC(1, sizeof(iob_interval_t), gf_iob_mt_interval);

    iob_interval_t *last_interval;
    iob_conf_t *conf = &(((iob_buffer_t *) (this->private))->conf);
    int i;
    struct rb_traverser traverser;


    interval->iov = iov_dup(vector, count);
    interval->count = count;
    interval->off = offset;

    for (i = 0; i < count; i++)
        iobuf_ref(iobref->iobrefs[i]);

    iobref_ref(iobref);


    interval->iobref = iobref;
    interval->length = iov_length(vector, count);


    LOCK(&inode_buffer->lock);
    last_interval = rb_find_max(vectors, vector);

    //If any intervals interleaved, flush the file.
    if (last_interval != NULL) {
        if (last_interval->off + last_interval->length > offset)
            iob_flush_buffer(frame, this, fd, inode_buffer,GF_FOP_WRITE);

        //TODO:judge whether there is a buffer behind this buffer.
        //FIXME: Bug exists here.
/*
        rb_t_find(&traverser,vectors,last_interval);

        if((last_interval=rb_t_next(&traverser))!=NULL){
            if(last_interval->off < offset + interval->length)
                iob_flush_buffer(frame,this,fd,inode_buffer);
        }
*/
    }

    rb_insert(vectors, interval);

    inode_buffer->buffered_size += interval->length;

    if (inode_buffer->buffered_size >= conf->batch_size)
        iob_flush_buffer(frame, this, fd, inode_buffer,GF_FOP_WRITE);

    UNLOCK(&inode_buffer->lock);


}

int
mem_acct_init(xlator_t *this) {
    int ret = -1;

    if (!this) {
        goto out;
    }

    ret = xlator_mem_acct_init(this, gf_iob_mt_end + 1);

    if (ret != 0) {
        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                IO_BATCH_MEMORY_INIT_FAILED,
                "Memory accounting init"
                        "failed");
    }

    out:
    return ret;
}


int
iob_writev_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno,
               struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata) {


    iob_frame_t *iob_frame = cookie;
    struct iobref *bref = NULL;
    iob_shared_t *shared;
    int i;
    gf_boolean_t finished;
    call_frame_t *cbk_frame;

    if (!iob_frame)
        return 0;

    bref = iob_frame->iobref;

    if (bref) {
        for (i = 0; i < bref->alloced; i++) {
            struct iobuf *buf = bref->iobrefs[i];
            if (!buf)
                break;
            iobuf_unref(buf);
        }
        iobref_unref(bref);
    }


    shared = iob_frame->shared;

    assert(shared != NULL);


    if (op_ret < 0) {
        LOCK(&shared->mutex);
        shared->op_ret = op_ret;
        shared->op_errno = op_errno;
        UNLOCK(&shared->mutex);
    }

    LOCK(&shared->mutex);

    if (--shared->ref == 0) {

        if (iob_frame->fop == GF_FOP_FLUSH) {

            if (shared->op_ret < 0) {
                STACK_UNWIND_STRICT(flush, iob_frame->frame, op_ret, op_errno, NULL);
            } else {
                STACK_WIND (iob_frame->frame, default_flush_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->flush, iob_frame->fd, iob_frame->xdata);
            }
        }
        finished = _gf_true;
    }

    assert(frame->root != NULL);
    STACK_DESTROY(frame->root);


    UNLOCK(&shared->mutex);

    //There will be only one outstanding thread with ref 0 because of the lock above.
    if (finished)
        GF_FREE(shared);

    GF_FREE(iob_frame);

    return 0;
}


int
iob_writev(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t offset, uint32_t flags, struct iobref *iobref,
           dict_t *xdata) {

    iob_conf_t *conf = NULL;
    iob_buffer_t *buffer = NULL;
    char *gfid = NULL;
    data_t *data = NULL;
    iob_buffer_inode_t *inode_buffer = NULL;
    uint32_t op_ret = 0;
    uint32_t op_erro = EINVAL;
    struct iatt iatt_buf = {0,};

    buffer = this->private;
    conf = &(buffer->conf);

    if (!fd || !fd->inode || !fd->inode->gfid)
        goto out;


    gfid = uuid_utoa(fd->inode->gfid);

    //Lock is needed to prevent from creating an buffer twice for a single file.
    LOCK(&buffer->lock);
    data = dict_get(buffer->buffer_dic, gfid);

    if (data != NULL) {
        //File found. Insert the iovec into file buffer.
        inode_buffer = (iob_buffer_inode_t *) (data->data);
        UNLOCK(&buffer->lock);
        op_ret = insert_vector(frame, this, fd, inode_buffer, offset, vector, count, iobref);
    } else {
        inode_buffer = GF_CALLOC(1, sizeof(iob_buffer_inode_t), gf_iob_mt_buffer_file);
        inode_buffer->buffered_size = 0;
        inode_buffer->vectors = rb_create((rb_comparison_func *) iob_buffer_vector_comparator, NULL, NULL);
        LOCK_INIT(&inode_buffer->lock);
        dict_add(buffer->buffer_dic, gfid, data_from_ptr(inode_buffer));
        UNLOCK(&buffer->lock);
        op_ret = insert_vector(frame, this, fd, inode_buffer, offset, vector, count, iobref);

    }


    STACK_UNWIND_STRICT (writev, frame, vector[0].iov_len, 0, &iatt_buf, &iatt_buf,
                         NULL);
    return 0;

    out:
    STACK_UNWIND_STRICT (writev, frame, -1, EINVAL, NULL, NULL, NULL);
    return 0;
}


int
iob_flush(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata) {
    iob_conf_t *conf = NULL;
    iob_buffer_t *buffer = NULL;
    char *gfid = NULL;
    data_t *data = NULL;
    iob_buffer_inode_t *inode_buffer = NULL;
    uint32_t op_ret = 0;
    uint32_t op_erro = EINVAL;
    struct iatt iatt_buf = {0,};
    call_frame_t *bg_frame = NULL;
    int total;

    buffer = this->private;
    conf = &(buffer->conf);
    if (!fd || !fd->inode || !fd->inode->gfid)
        goto out;

    gfid = uuid_utoa(fd->inode->gfid);



    data = dict_get(buffer->buffer_dic, gfid);

    if (data != NULL) {
        //File found. Insert the iovec into file buffer.
        inode_buffer = (iob_buffer_inode_t *) (data->data);
        LOCK(&inode_buffer->lock);
        total = iob_flush_buffer(frame, this, fd, inode_buffer,GF_FOP_FLUSH);
        UNLOCK(&inode_buffer->lock);
        if (!total) {
            //No buffer,fall through.
            STACK_WIND (frame, default_flush_cbk, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->flush, fd, xdata);
        }else{

        }

    } else {
        //Fall through.
        STACK_WIND (frame, default_flush_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->flush, fd, xdata);
    }

    return 0;


    out:
    STACK_UNWIND_STRICT (flush, frame, -1, EINVAL, xdata);
    return 0;
}


struct xlator_fops fops = {
        .writev      = iob_writev,
        .flush       = iob_flush
};


struct xlator_cbks cbks = {

};