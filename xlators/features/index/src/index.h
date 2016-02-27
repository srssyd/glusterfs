/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __INDEX_H__
#define __INDEX_H__

#include "xlator.h"
#include "call-stub.h"
#include "defaults.h"
#include "byte-order.h"
#include "common-utils.h"
#include "index-mem-types.h"

#define INDEX_THREAD_STACK_SIZE   ((size_t)(1024*1024))

typedef enum {
        UNKNOWN,
        IN,
        NOTIN
} index_state_t;

typedef enum {
        PENDING,
        DIRTY,
        XATTROP_TYPE_END
} index_xattrop_type_t;

typedef struct index_inode_ctx {
        gf_boolean_t processing;
        struct list_head callstubs;
        int state[XATTROP_TYPE_END];
} index_inode_ctx_t;

typedef struct index_fd_ctx {
        DIR *dir;
        off_t dir_eof;
} index_fd_ctx_t;

typedef struct index_priv {
        char *index_basepath;
        char *dirty_basepath;
        uuid_t index;
        gf_lock_t lock;
        uuid_t xattrop_vgfid;//virtual gfid of the xattrop index dir
        uuid_t dirty_vgfid; // virtual gfid of the on-going/dirty index dir
        struct list_head callstubs;
        pthread_mutex_t mutex;
        pthread_cond_t  cond;
        dict_t  *dirty_watchlist;
        dict_t  *pending_watchlist;
        dict_t  *complete_watchlist;
        int64_t  pending_count;
} index_priv_t;

#define INDEX_STACK_UNWIND(fop, frame, params ...)      \
do {                                                    \
        if (frame) {                                    \
                inode_t *_inode = frame->local;         \
                frame->local = NULL;                    \
                inode_unref (_inode);                   \
        }                                               \
        STACK_UNWIND_STRICT (fop, frame, params);       \
} while (0)

#endif
