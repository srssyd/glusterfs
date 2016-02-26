/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.
  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "ec-method.h"




int64_t ec_method_batch_encode_cuda(size_t size, uint32_t columns, uint32_t total_rows,
                                   uint8_t * in, uint8_t ** out)
{
    return -1;
}



int64_t ec_method_decode_cuda(size_t size, uint32_t columns, uint32_t * rows,
                             uint8_t ** in, uint8_t * out)
{
    return -1;
}