//
// Created by syd on 16-6-28.
//

#ifndef IDA_RABIN_EC_WORKER_H
#define IDA_RABIN_EC_WORKER_H
#define _GNU_SOURCE

#include <sched.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <semaphore.h>
#include <sys/sysinfo.h>
#include <assert.h>
#include <stdbool.h>

typedef struct fast_sem {
    pthread_mutex_t guard;
    pthread_cond_t cond;
    int count;
} fast_sem_t;

static void fast_sem_init(fast_sem_t *s) {
    pthread_mutex_init(&s->guard, 0);
    pthread_cond_init(&s->cond, 0);
    s->count = 0;
}

static void incr(fast_sem_t *s, unsigned delta) {
    assert(s);
    pthread_mutex_lock(&s->guard);
    s->count += delta;
    pthread_cond_broadcast(&s->cond);
    pthread_mutex_unlock(&s->guard);
}

static void decr(fast_sem_t *s, unsigned delta) {
    assert(s);
    pthread_mutex_lock(&s->guard);
    do {
        if (s->count >= delta) {
            s->count -= delta;
            break;
        }
        pthread_cond_wait(&s->cond, &s->guard);
    } while (1);
    pthread_mutex_unlock(&s->guard);
}


typedef struct _workers *worker_pool_t;

struct _workers {
    pthread_t *threads;
    int num_of_threads;

    pthread_mutex_t lock;
    pthread_barrier_t barrier;

    pthread_mutex_t cond_lock;
    pthread_cond_t cond;

    fast_sem_t job;
    sem_t finished;

    void *datas;

    void (*func)(void *);

    int param_size;
};


void *worker_thread(void *pool) {
    worker_pool_t workers = (worker_pool_t) pool;
    int myid = 0;
    for (; myid < workers->num_of_threads; myid++)
        if (pthread_self() == workers->threads[myid])
            break;

    while (1) {
        //printf("My id is:%d,waiting\n",myid);

        decr(&workers->job,1);
        //printf("My id is:%d,begin to work\n",myid);
        (*workers->func)(workers->datas + myid * workers->param_size);
        //printf("My id is:%d,finished\n",myid);
        pthread_barrier_wait(&workers->barrier);
        if (myid == 0)
            sem_post(&workers->finished);
    }

}

worker_pool_t worker_pool_init(int num_of_threads) {
    int i;
    assert(num_of_threads <= get_nprocs());

    worker_pool_t result = (worker_pool_t) malloc(sizeof(struct _workers));
    result->num_of_threads = num_of_threads;
    result->threads = (pthread_t *) malloc(sizeof(pthread_t) * num_of_threads);

    pthread_attr_t attr;
    cpu_set_t cpus;
    pthread_attr_init(&attr);

    pthread_cond_init(&result->cond, NULL);
    fast_sem_init(&result->job);
    sem_init(&result->finished, 0, 0);

    pthread_barrier_init(&result->barrier, NULL, num_of_threads);


    for (i = 0; i < num_of_threads; i++) {
        CPU_ZERO(&cpus);
        CPU_SET(i, &cpus);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
        pthread_create(result->threads + i, &attr, worker_thread, result);
    }

    return result;
}

void worker_pool_run(worker_pool_t pool, void (*func)(void *), void *data, int param_size) {
    int i;
    pthread_mutex_lock(&pool->lock);
    pool->datas = data;
    pool->param_size = param_size;

    pool->func = func;

    incr(&pool->job,pool->num_of_threads);


    sem_wait(&pool->finished);
    pthread_mutex_unlock(&pool->lock);
}


#endif //IDA_RABIN_EC_WORKER_H
