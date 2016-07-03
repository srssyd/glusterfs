/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_METHOD_H__
#define __EC_METHOD_H__

#include "ec-gf.h"
#include <stdio.h>




/* Determines the maximum size of the matrix used to encode/decode data */
#define EC_METHOD_MAX_FRAGMENTS 64
/* Determines the maximum number of usable elements in the Galois Field */
#define EC_METHOD_MAX_NODES     (EC_GF_SIZE - 1)


#ifdef USE_AVX
#define EC_METHOD_WORD_SIZE 256
#else
#define EC_METHOD_WORD_SIZE 64
#endif

#define EC_METHOD_CHUNK_SIZE (EC_METHOD_WORD_SIZE * EC_GF_BITS)
#define EC_METHOD_WIDTH (EC_METHOD_WORD_SIZE / EC_GF_WORD_SIZE)

#define AUTO_PIPE 1
#define AUTO_THPOOL 1
#define THR_PIPELINE (128 * 1024 * 1024)
#define THR_THPOOL (1024 * 1024)


void ec_method_initialize(void);
size_t ec_method_encode(size_t size, uint32_t columns, uint32_t row,
                        uint8_t * in, uint8_t * out);
size_t ec_method_decode(size_t size, uint32_t columns, uint8_t * rows,
                        uint8_t ** in, uint8_t * out);
size_t ec_method_batch_encode(size_t size, uint32_t columns, uint32_t total_row, uint8_t * rows,
                              uint8_t * in, uint8_t ** out);

#endif /* __EC_METHOD_H__ */
