/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_GF8_H__
#define __EC_GF8_H__

#define EC_GF_BITS 8
#define EC_GF_MOD 0x11D

#define EC_GF_SIZE (1 << EC_GF_BITS)
#define USE_AVX

#ifdef USE_AVX
#include <immintrin.h>
#define encode_t __m256i
#define XOR3(A,B,C) (A) = _mm256_xor_si256((B),(C))
#define XOR4(A,B,C,D) (A) = _mm256_xor_si256(_mm256_xor_si256((B),(C)),(D))
#define XOR5(A,B,C,D,E) (A) = _mm256_xor_si256(_mm256_xor_si256((B),(C)),_mm256_xor_si256((D),(E)))
#else
#define encode_t uint64_t
#define XOR3(A,B,C) (A) = (B) ^ (C)
#define XOR4(A,B,C,D) (A) = (B) ^ (C) ^ (D)
#define XOR5(A,B,C,D,E) (A) = (B) ^ (C) ^ (D) ^ (E)
#endif


#define EC_GF_WORD_SIZE sizeof(encode_t)

#include <inttypes.h>

extern void (* ec_gf_muladd[])(uint8_t * out, uint8_t * in,
                               unsigned int width);

#endif /* __EC_GF8_H__ */
