//
// Created by syd on 16-8-26.
//

#include <defaults.h>
#include "xlator.h"
#include "io-batch-messages.h"
#include "contrib/rbtree/rb.h"
#include "io-batch.h"
#include "iob-mem-types.h"


/* Currently, md-cache should not be used together with io-batch */

/*
 * TODO: 1.flush all data if the total memory consumed reach a certain threshold.
 * TODO: 2.flush data periodically.
 * TODO: 3.passing this xlator for O_DSYNC,O_SYNC flag.
 * TODO: 4.handle xdata.
 * TODO: 5.handle truncate,fsync,stat.
 * TODO: 6:control the vector count for write.
 * TODO: 7:allow read and write at the same time(should be applied after fixing the performance of readv with small bs).
 * TODO: 8:the performance of iobref_merge is extremely slow, so there should not be too much vectors in one iobref.
 */

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
    return 0;
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
            result = p->rb_data,p = p->rb_link[1];
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

int get_total_inode_buffer_continuous_intervals(iob_buffer_inode_t *inode_buffer){
    struct rb_table *vectors = inode_buffer->vectors;
    iob_interval_t *interval_begin,*interval_end;
    struct rb_traverser traverser;
    off_t pre_off, begin_off;
    size_t pre_size;
    int result = 0;


    interval_begin = rb_t_first(&traverser, vectors);

    while (interval_begin) {

        while(interval_begin && interval_begin->status == BUFFERED){
            interval_begin = rb_t_next(&traverser);
        }
        if(interval_begin) {
            pre_off = interval_begin->off;
            pre_size = interval_begin->length;
            interval_end = rb_t_next(&traverser);
            result++;
            while (interval_end && interval_end->status!=BUFFERED && interval_end->off == pre_off + pre_size) {
                pre_off = interval_end->off;
                pre_size = interval_end->length;
                interval_end = rb_t_next(&traverser);
            }
            interval_begin = interval_end;
        }
    }

    return result;
}

//Not thread safe.
static int iob_flush_buffer(call_frame_t *frame, xlator_t *this, fd_t *fd, iob_buffer_inode_t *inode_buffer,glusterfs_fop_t fop) {
    struct rb_table *vectors = inode_buffer->vectors;
    struct rb_traverser begin_traveser, end_traverser;
    iob_interval_t *interval_begin,*interval_end,*tmp_interval;
    call_frame_t *bg_frame;
    off_t pre_off, begin_off;
    size_t pre_size;
    int vector_cnt;
    int32_t ret;
    int i, j, cnt;
    iob_write_frame_t *iob_frame;
    iob_shared_t *shared = GF_CALLOC(1, sizeof(iob_shared_t), gf_iob_mt_shared);
    int total = 0;
    iob_buffer_t * buffers = this->private;

    LOCK_INIT(&shared->lock);
    shared->ref = 0;
    shared->op_ret = 0;
    shared->op_errno = 0;

    interval_begin = rb_t_first(&begin_traveser, vectors);
    end_traverser = begin_traveser;

    total = get_total_inode_buffer_continuous_intervals(inode_buffer);

    LOCK(&shared->lock);
    shared->ref = shared->ref + total;
    UNLOCK(&shared->lock);


    while (interval_begin) {
        while(interval_begin && interval_begin->status == BUFFERED){
            tmp_interval = rb_t_next(&begin_traveser);
            rb_delete(vectors,interval_begin);


            //Memory leak exists here. The memory is not freed completely.
            for(i=0;i <interval_begin->iobref->alloced;i++) {
                if(!interval_begin->iobref->iobrefs[i])
                    break;
                iobuf_unref(interval_begin->iobref->iobrefs[i]);
            }

            iobref_unref(interval_begin->iobref);

            GF_FREE(interval_begin);
            interval_begin = tmp_interval;
        }
        if(interval_begin) {
            pre_off = interval_begin->off;
            begin_off = pre_off;
            pre_size = interval_begin->length;
            vector_cnt = interval_begin->count;
            interval_end = rb_t_next(&end_traverser);

            while (interval_end && interval_end->status != BUFFERED && interval_end->off == pre_off + pre_size) {
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

                //There should not be too much vectors merged.
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

            free(intervals);


            bg_frame = copy_frame(frame);
            assert(bg_frame != NULL);

            iob_frame = GF_CALLOC(1, sizeof(iob_write_frame_t), gf_iob_mt_write_frame);
            iob_frame->iobref = iobref;
            iob_frame->frame = frame;
            iob_frame->fd = fd;
            iob_frame->shared = shared;
            iob_frame->fop = fop;
            iob_frame->xdata = NULL;


            STACK_WIND_COOKIE (bg_frame, iob_writev_cbk, iob_frame,
                               FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev,
                               fd, vector_batched, vector_cnt, begin_off, 0, iobref, NULL);
        }

    }

    assert(rb_count(vectors) == 0);
    inode_buffer->buffered_size = 0;

    return total;
}

//TODO: Handle different flag.
//TODO: Save the xdata.

//Not thread safely.
static void
insert_vector(call_frame_t *frame, xlator_t *this, fd_t *fd, iob_buffer_inode_t *inode_buffer, off_t offset,
              struct iovec *vector, uint32_t count, struct iobref *iobref,iob_interval_status_t status) {
    if (count <= 0)
        return ;

    struct rb_table *vectors = inode_buffer->vectors;
    iob_interval_t *interval = GF_CALLOC(1, sizeof(iob_interval_t), gf_iob_mt_interval);

    iob_interval_t *last_interval,*next_interval;
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
    interval->status = status;



        last_interval = rb_find_max(vectors, interval);

        //If any intervals interleaved, flush the file.
        if (last_interval != NULL) {
            if (last_interval->off + last_interval->length > offset)
                iob_flush_buffer(frame, this, fd, inode_buffer, GF_FOP_WRITE);


            rb_t_find(&traverser, vectors, last_interval);
            //assert(traverser.rb_node != NULL);


            next_interval = rb_t_next(&traverser);

            assert(next_interval != last_interval);

            if (next_interval != NULL) {
                if (next_interval->off < offset + interval->length)
                    iob_flush_buffer(frame, this, fd, inode_buffer, GF_FOP_WRITE);
            }

        }

        //if(status == WRITTEN)


        if (inode_buffer->buffered_size >= conf->batch_size)
            iob_flush_buffer(frame, this, fd, inode_buffer, GF_FOP_WRITE);

        inode_buffer->buffered_size += interval->length;

        rb_insert(vectors, interval);
}

void get_next_prefetch_size(off_t offset,size_t size,off_t *new_off,size_t *new_size){
    //*new_off = offset;
    *new_size = min(128 * 1u<<20,size * 2);
}

static void read_vector(call_frame_t *frame, xlator_t *this, iob_buffer_inode_t *inode_buffer,fd_t *fd, size_t size,
                        off_t offset, uint32_t flags, dict_t *xdata){

    //printf("Request off:%u size:%u\n",offset,size);

    iob_interval_t * interval,current;
    iob_read_status_t status;
    iob_read_frame_t    *read_frame;

    read_frame = GF_CALLOC(1,sizeof(iob_read_frame_t),gf_iob_mt_read_frame);
    assert(read_frame != NULL);

    current.off = offset;
    current.length = size;
    assert(inode_buffer != NULL);
    pthread_mutex_lock(&inode_buffer->mutex);
    {
        interval = rb_find_max(inode_buffer->vectors,&current);
        if(interval){
            if(interval->off + interval->length < offset){
                //Simple read.
                status = SIMPLE;
            }else if(interval->off + interval->length > offset){
                if(interval->off + interval->length >= offset + size){
                    //What we need read is already in cache.Read from buffer directly.
                    status = CACHED;
                }else{
                    //The data we need is in buffer partially,some is not.
                    //Two possibilities.
                    //Data read partially,or the end of the file has been reached.
                    status = PARTIAL;
                }
            }else{
                //Try to prefetch the next interval.
                status = PREFETCH;
            }
        }else{
            //Simple read.
            status = SIMPLE;
        }

        read_frame->status = status;
        read_frame->frame = frame;
        read_frame->lock = &(inode_buffer->mutex);
        read_frame->size = size;
        read_frame->offset = offset;
        read_frame->inode_buffer = inode_buffer;

        if(status == SIMPLE){
            printf("SIMPLE off:%u size:%u\n",offset,size);
            STACK_WIND_COOKIE(frame,iob_readv_cbk,read_frame,FIRST_CHILD(this),FIRST_CHILD(this)->fops->readv,fd,size,offset,flags,xdata);
        }else if(status == CACHED) {
            iob_readv_cbk(frame,read_frame,FIRST_CHILD(this),size,0,NULL,0,NULL,NULL,NULL);

        }else if(status == PREFETCH){
            off_t new_off = offset;
            size_t new_size;
            get_next_prefetch_size(interval->off,interval->length,&new_off,&new_size);
            new_size = max(new_size,size);
            printf("PREFETCH off:%u size:%u\n",new_off,new_size);
            STACK_WIND_COOKIE(frame,iob_readv_cbk,read_frame,FIRST_CHILD(this),FIRST_CHILD(this)->fops->readv,fd,new_size,new_off,flags,xdata);

        }else if(status == PARTIAL){
            //Flush to reduce complexity. Should be improved.
            printf("PARTIAL off:%u size:%u\n",offset,size);
            iob_flush_buffer(frame,this,fd,inode_buffer,GF_FOP_WRITE);
            read_frame->status = SIMPLE;
            STACK_WIND_COOKIE(frame,iob_readv_cbk,read_frame,FIRST_CHILD(this),FIRST_CHILD(this)->fops->readv,fd,size,offset,flags,xdata);
        }


    }
    //Lock should be unlocked in the callback procedure to prevent from unexpected failure.


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



int iob_readv_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                     int32_t op_ret, int32_t op_errno, struct iovec * vector,
                     int32_t count, struct iatt * stbuf,
                     struct iobref * iobref, dict_t * xdata){

    iob_read_frame_t *read_frame = cookie;
    iob_interval_t  *interval,current;
    struct iatt buf;
    if(read_frame->status == CACHED){

        //Data should have been cached.

        struct iovec   * vec;

        vec = GF_CALLOC(1,sizeof(struct iovec),gf_common_mt_iovec);


        vec->iov_len = read_frame->size;

        current.off = read_frame->offset;
        current.length = read_frame->size;

        assert(read_frame->inode_buffer != NULL);

        interval = rb_find_max(read_frame->inode_buffer->vectors,&current);

        assert(interval->off <= current.off);
        assert(interval->off + interval->length >= current.off + current.length);

        vec->iov_base = interval->iobref->iobrefs[0]->ptr + (current.off - interval->off);
        pthread_mutex_unlock(read_frame->lock);
        STACK_UNWIND_STRICT(readv,frame,op_ret,op_errno,vec,1,&buf,iobref,xdata);

    }else if(read_frame->status == SIMPLE || read_frame->status == PREFETCH){
        if(op_ret >=0){

                //The block to be read is larger than the file size.
                //FIXME: fd should not be null, or data in the buffer will be lost.
                insert_vector(frame,this,NULL,read_frame->inode_buffer,read_frame->offset,vector,count,iobref,BUFFERED);

                struct iobref  *bref;
                struct iobuf   * buf;
                struct iovec   * vec;
                size_t      new_size = min(op_ret,read_frame->size);
                bref = iobref_new();
                buf = iobuf_get2(this->ctx->iobuf_pool,new_size);
                iobref_add(bref,buf);
                vec = GF_CALLOC(1,sizeof(struct iovec),gf_common_mt_iovec);

                vec->iov_base = buf->ptr;
                vec->iov_len = new_size;

                current.off = read_frame->offset;
                current.length = vec->iov_len;

                assert(read_frame->inode_buffer != NULL);

                interval = rb_find_max(read_frame->inode_buffer->vectors,&current);

                assert(interval != NULL);
                assert(interval->off <= current.off);
                assert(interval->off + interval->length >= current.off + current.length);

                memcpy(buf->ptr,interval->iobref->iobrefs[0]->ptr + (current.off - interval->off),vec->iov_len);
                pthread_mutex_unlock(read_frame->lock);
                STACK_UNWIND_STRICT(readv,frame,new_size,op_errno,vec,1,stbuf,bref,xdata);

        }else{
            pthread_mutex_unlock(read_frame->lock);
            STACK_UNWIND_STRICT(readv,frame,op_ret,op_errno,vector,count,stbuf,iobref,xdata);
        }
    }else if(read_frame->status == PARTIAL){
        //Not possible now.
        assert(0);
    }


}



int
iob_writev_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno,
               struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata) {


    iob_write_frame_t *iob_frame = cookie;
    struct iobref *bref = NULL;
    iob_shared_t *shared;
    int i;
    gf_boolean_t finished = _gf_false;
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
        LOCK(&shared->lock);
        shared->op_ret = op_ret;
        shared->op_errno = op_errno;
        UNLOCK(&shared->lock);
    }

    LOCK(&shared->lock);

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

    UNLOCK(&shared->lock);

    assert(frame->root != NULL);
    STACK_DESTROY(frame->root);

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
        pthread_mutex_lock(&inode_buffer->mutex);
        insert_vector(frame, this, fd, inode_buffer, offset, vector, count, iobref,WRITTEN);
        pthread_mutex_unlock(&inode_buffer->mutex);
    } else {
        inode_buffer = GF_CALLOC(1, sizeof(iob_buffer_inode_t), gf_iob_mt_buffer_file);
        inode_buffer->buffered_size = 0;
        inode_buffer->vectors = rb_create((rb_comparison_func *) iob_buffer_vector_comparator, NULL, NULL);
        pthread_mutex_init(&inode_buffer->mutex,NULL);
        dict_add(buffer->buffer_dic, gfid, data_from_ptr(inode_buffer));
        UNLOCK(&buffer->lock);
        pthread_mutex_lock(&inode_buffer->mutex);
        insert_vector(frame, this, fd, inode_buffer, offset, vector, count, iobref,WRITTEN);
        pthread_mutex_unlock(&inode_buffer->mutex);

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


    //Do not need lock because lock is inside the dict.
    data = dict_get(buffer->buffer_dic, gfid);

    if (data != NULL) {
        //File found. Insert the iovec into file buffer.
        inode_buffer = (iob_buffer_inode_t *) (data->data);

        pthread_mutex_lock(&inode_buffer->mutex);
        {
            total = iob_flush_buffer(frame, this, fd, inode_buffer, GF_FOP_FLUSH);
        }
        pthread_mutex_unlock(&inode_buffer->mutex);

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

int
iob_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset, uint32_t flags, dict_t *xdata){
    iob_buffer_t * buffer = this->private;
    char         * gfid  = NULL;
    data_t       * data  = NULL;
    iob_buffer_inode_t *inode_buffer = NULL;
    if(!fd || !fd->inode || !fd->inode->gfid)
        goto out;

    gfid = uuid_utoa(fd->inode->gfid);

    LOCK(&buffer->lock);
    data = dict_get(buffer->buffer_dic, gfid);

    if(data != NULL){
        inode_buffer = (iob_buffer_inode_t *)data->data;
        UNLOCK(&buffer->lock);
        read_vector(frame,this,inode_buffer,fd,size,offset,flags,xdata);

    }else{
        inode_buffer = GF_CALLOC(1, sizeof(iob_buffer_inode_t), gf_iob_mt_buffer_file);
        inode_buffer->buffered_size = 0;
        inode_buffer->vectors = rb_create((rb_comparison_func *) iob_buffer_vector_comparator, NULL, NULL);
        pthread_mutex_init(&inode_buffer->mutex,NULL);
        dict_add(buffer->buffer_dic, gfid, data_from_ptr(inode_buffer));
        UNLOCK(&buffer->lock);
        read_vector(frame,this,inode_buffer,fd,size,offset,flags,xdata);
    }
    out:
        return 0;

}

struct xlator_fops fops = {
        .writev      = iob_writev,
        .flush       = iob_flush,
        .readv       = iob_readv
};


struct xlator_cbks cbks = {

};