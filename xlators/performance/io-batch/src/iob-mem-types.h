//
// Created by syd on 16-8-27.
//

#ifndef _IOB_MEM_TYPES_H
#define _IOB_MEM_TYPES_H

#include <mem-types.h>

enum gf_iob_mem_types_ {
    gf_iob_mt_buffers  = gf_common_mt_end + 1,
    gf_iob_mt_conf,
    gf_iob_mt_buffer_file,
    gf_iob_mt_interval,
    gf_iob_mt_end
};


#endif //GLUSTERFS_IOB_MEM_TYPES_H
