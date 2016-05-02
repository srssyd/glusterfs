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
#include <pthread.h>
#include "ec-gf.h"
#include "ec-method.h"
#include "thpool.h"
#include "ec-common.h"

static uint32_t GfPow[EC_GF_SIZE << 1];
static uint32_t GfLog[EC_GF_SIZE << 1];
static threadpool thpool;
static int processor_count;
void ec_method_initialize(int process_count)
{
    uint32_t i;

    GfPow[0] = 1;
    GfLog[0] = EC_GF_SIZE;
    for (i = 1; i < EC_GF_SIZE; i++)
    {
        GfPow[i] = GfPow[i - 1] << 1;
        if (GfPow[i] >= EC_GF_SIZE)
        {
            GfPow[i] ^= EC_GF_MOD;
        }
        GfPow[i + EC_GF_SIZE - 1] = GfPow[i];
        GfLog[GfPow[i] + EC_GF_SIZE - 1] = GfLog[GfPow[i]] = i;
    }

    thpool = thpool_init(process_count);
    processor_count=process_count;
}

static uint32_t ec_method_mul(uint32_t a, uint32_t b)
{
    if (a && b)
    {
        return GfPow[GfLog[a] + GfLog[b]];
    }

    return 0;
}

static uint32_t ec_method_div(uint32_t a, uint32_t b)
{
    if (b)
    {
        if (a)
        {
            return GfPow[EC_GF_SIZE - 1 + GfLog[a] - GfLog[b]];
        }
        return 0;
    }
    return EC_GF_SIZE;
}

struct ec_encode_param{
    size_t size;
    uint32_t columns, row;
    uint8_t * in, * out;
};
typedef struct ec_encode_param ec_encode_param_t;

static void* ec_method_single_encode(void * param){

    uint32_t i, j;
    ec_encode_param_t *ec_param = (ec_encode_param_t *)param;
    size_t size = ec_param->size;
    uint32_t columns = ec_param->columns;
    uint32_t row = ec_param->row;
    uint8_t *in = ec_param->in;
    uint8_t *out = ec_param->out;

    for (j = 0; j < size; j++)
    {
        ec_gf_muladd[0](out, in, EC_METHOD_WIDTH);
        in += EC_METHOD_CHUNK_SIZE;
        for (i = 1; i < columns; i++)
        {
            ec_gf_muladd[row](out, in, EC_METHOD_WIDTH);
            in += EC_METHOD_CHUNK_SIZE;
        }
        out += EC_METHOD_CHUNK_SIZE;
    }
}

size_t ec_method_encode(size_t size, uint32_t columns, uint32_t row, uint8_t * in, uint8_t * out)
{
    uint32_t i, j;
    uint8_t * in_ptr=in,*out_ptr=out;
    size_t ori_size = size;
    pthread_t *threads = malloc(sizeof(pthread_t)*processor_count);
    ec_encode_param_t *params = malloc(sizeof(ec_encode_param_t)*processor_count);
    size /= EC_METHOD_CHUNK_SIZE * columns;
    row++;

    for(i=0;i<processor_count;i++){
        params[i] = (ec_encode_param_t){
            .size = size/processor_count + (i< (size%processor_count)),
                .columns = columns,
                .row = row,
                .in = in_ptr,
                .out = out_ptr
        };
        //printf("%u %x\n",param.size,param.in);
        // printf("%x\n",in);
        in_ptr += EC_METHOD_CHUNK_SIZE * params[i].size * columns;
        out_ptr += EC_METHOD_CHUNK_SIZE * params[i].size;
#if AUTO_THPOOL
        if (ori_size < THR_THPOOL) {
            ec_method_single_encode((void *)(params + i));
        }
        else {
            thpool_add_work(thpool,ec_method_single_encode,(void *)(params+i));
        }
#else
        thpool_add_work(thpool, ec_method_single_encode, (void *)(params + i));
#endif
    }
#if AUTO_THPOOL
    if (ori_size >= THR_THPOOL) {
        thpool_wait(thpool);
    }
#else
    thpool_wait(thpool);
#endif
    free(params);
    return size * EC_METHOD_CHUNK_SIZE;
}
struct ec_encode_batch_param{
    size_t size;
    uint32_t columns, total_rows, off;
    uint8_t * in;
    uint8_t * rows;
    uint8_t ** out;
};
typedef struct ec_encode_batch_param ec_encode_batch_param_t;
static void* ec_method_batch_single_encode(void * param)
{
    uint32_t i, j,row;
    ec_encode_batch_param_t *ec_param = (ec_encode_batch_param_t *)param;
    size_t size = ec_param->size;
    uint32_t columns = ec_param->columns;
    uint32_t total_row = ec_param->total_rows;
    uint32_t off = ec_param->off;
    uint8_t *in = ec_param->in,*in_ptr=NULL;
    uint8_t **out = ec_param->out;
    uint8_t *rows = ec_param->rows;

    for(j = 0;j < size; j++){
        for (row = 0;row < total_row;row++){
            in_ptr = in;
            ec_gf_muladd[0](out[row]+off+j*EC_METHOD_CHUNK_SIZE, in_ptr, EC_METHOD_WIDTH);
            in_ptr += EC_METHOD_CHUNK_SIZE;
            for (i = 1; i < columns; i++)
            {
                ec_gf_muladd[rows[row]+1](out[row]+off+j*EC_METHOD_CHUNK_SIZE, in_ptr, EC_METHOD_WIDTH);
                in_ptr += EC_METHOD_CHUNK_SIZE;
            }
        }
        in += EC_METHOD_CHUNK_SIZE * columns;
    }
}
size_t ec_method_batch_encode(size_t size, uint32_t columns, uint32_t total_rows, uint8_t * rows, uint8_t * in, uint8_t ** out)
{
    uint32_t i, j,off;
    size_t ori_size = size;
    ec_encode_batch_param_t *params = malloc(sizeof(ec_encode_batch_param_t)*processor_count);
    size /= EC_METHOD_CHUNK_SIZE * columns;
    off=0;
#if AUTO_THPOOL
    if(ori_size < THR_THPOOL){
        params[0] = (ec_encode_batch_param_t){
            .size = size,
                .columns = columns,
                .total_rows = total_rows,
                .in = in,
                .out = out,
                .off = off
        };
        ec_method_batch_single_encode((void*)params);
    }else{
        for(i=0;i<processor_count;i++){
            params[i] = (ec_encode_batch_param_t){
                .size = size/processor_count + (i< (size%processor_count)),
                    .columns = columns,
                    .total_rows = total_rows,
                    .in = in,
                    .out = out,
                    .off = off
            };
            in += EC_METHOD_CHUNK_SIZE * params[i].size * columns;
            off += EC_METHOD_CHUNK_SIZE * params[i].size;
            thpool_add_work(thpool,ec_method_batch_single_encode,(void *)(params+i));
        }
        thpool_wait(thpool);
    }
#else
    for(i=0;i<processor_count;i++){
        params[i] = (ec_encode_batch_param_t){
            .size = size/processor_count + (i< (size%processor_count)),
                .columns = columns,
                .total_rows = total_rows,
                .in = in,
                .out = out,
                .off = off,
                .rows = rows
        };
        in += EC_METHOD_CHUNK_SIZE * params[i].size * columns;
        off += EC_METHOD_CHUNK_SIZE * params[i].size;
        thpool_add_work(thpool,ec_method_batch_single_encode,(void *)(params+i));
    }
    thpool_wait(thpool);
#endif

    free(params);
    return size * EC_METHOD_CHUNK_SIZE;
}


struct ec_decode_param{
    size_t size;
    uint32_t columns;
    uint32_t off;
    uint8_t ** in, * out;
    uint8_t *dummy;
    uint8_t **inv;
};
typedef struct ec_decode_param ec_decode_param_t;

void * ec_method_single_decode(void *param)
{
    ec_decode_param_t * ec_param = (ec_decode_param_t *)param;
    size_t size = ec_param->size;
    uint32_t columns = ec_param->columns;
    uint32_t off = ec_param->off;
    uint8_t **in = ec_param->in;
    uint8_t *out = ec_param->out;
    uint8_t *dummy = ec_param->dummy;
    uint8_t **inv = ec_param->inv;
    uint32_t i,j,k,last,value,f;
    for (f = 0; f < size; f++)
    {
        for (i = 0; i < columns; i++)
        {
            last = 0;
            j = 0;
            do
            {
                while (inv[i][j] == 0)
                {
                    j++;
                }
                if (j < columns)
                {
                    value = ec_method_div(last, inv[i][j]);
                    last = inv[i][j];
                    ec_gf_muladd[value](out, in[j] + off, EC_METHOD_WIDTH);
                    j++;
                }
            } while (j < columns);
            ec_gf_muladd[last](out, dummy, EC_METHOD_WIDTH);
            out += EC_METHOD_CHUNK_SIZE;
        }
        off += EC_METHOD_CHUNK_SIZE;
    }
}

size_t ec_method_decode(size_t size, uint32_t columns, uint32_t * rows,
        uint8_t ** in, uint8_t * out)
{
    uint32_t i, j, k, off, last, value;
    uint32_t f;
    uint8_t **inv;
    uint8_t **mtx;
    uint8_t *dummy;

    size /= EC_METHOD_CHUNK_SIZE;

    //Use some tricks to allocate 2-d array which is cache-friendly.
    inv = (uint8_t **)malloc(sizeof(uint8_t *) *columns);
    mtx = (uint8_t **)malloc(sizeof(uint8_t *) *columns);
    dummy = malloc(EC_METHOD_CHUNK_SIZE * sizeof(uint8_t));

    inv[0] = (uint8_t *)malloc((columns + 1)*columns * sizeof(uint8_t));
    mtx[0] = (uint8_t *)malloc(columns*columns * sizeof(uint8_t ));

    for(i=0;i<columns;i++)
        inv[i] = (*inv + (columns+1) * i),mtx[i]=(*mtx + columns * i);


    for(i=0;i<columns;i++){
        for(j=0;j<columns;j++)
            inv[i][j]=mtx[i][j]=0;
        inv[i][columns] = 0;
    }
    for(i=0;i<EC_METHOD_CHUNK_SIZE;i++)
        dummy[i]=0;

    for (i = 0; i < columns; i++)
    {
        inv[i][i] = 1;
        inv[i][columns] = 1;
    }
    for (i = 0; i < columns; i++)
    {
        mtx[i][columns - 1] = 1;
        for (j = columns - 1; j > 0; j--)
        {
            mtx[i][j - 1] = ec_method_mul(mtx[i][j], rows[i] + 1);
        }
    }

    for (i = 0; i < columns; i++)
    {
        f = mtx[i][i];
        for (j = 0; j < columns; j++)
        {
            mtx[i][j] = ec_method_div(mtx[i][j], f);
            inv[i][j] = ec_method_div(inv[i][j], f);
        }
        for (j = 0; j < columns; j++)
        {
            if (i != j)
            {
                f = mtx[j][i];
                for (k = 0; k < columns; k++)
                {
                    mtx[j][k] ^= ec_method_mul(mtx[i][k], f);
                    inv[j][k] ^= ec_method_mul(inv[i][k], f);
                }
            }
        }
    }

    ec_decode_param_t *params = malloc(sizeof(ec_decode_param_t)*processor_count);

    off = 0;
    for(i=0;i<processor_count;i++){
        params[i] = (ec_decode_param_t){
            .size = size/processor_count + (i< (size%processor_count)),
                .columns = columns,
                .off = off,
                .dummy = dummy,
                .inv = inv,
                .in = in,
                .out = out
        };
        off += EC_METHOD_CHUNK_SIZE * params[i].size;
        out += EC_METHOD_CHUNK_SIZE * params[i].size * columns;
        thpool_add_work(thpool,ec_method_single_decode,(void *)(params+i));
    }

    thpool_wait(thpool);

    free(params);
    free(dummy);
    free(inv[0]);
    free(mtx[0]);
    free(inv);
    free(mtx);

    return size * EC_METHOD_CHUNK_SIZE * columns;
}
