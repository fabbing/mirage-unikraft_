
/* SPDX-License-Identifier: MIT */
/*
 * Authors: Fabrice Buoro <fabrice@tarides.com>
 *          Samuel Hym <samuel@tarides.com>
 *
 * Copyright (c) 2024-2025, Tarides.
 *               All rights reserved.
*/

#ifdef __Unikraft__

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>

#include <caml/alloc.h>
#include <caml/memory.h>
#include <caml/mlvalues.h>

#include "yield.h"

pthread_mutex_t ready_sets_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ready_sets_cond = PTHREAD_COND_INITIALIZER;
uint64_t netdev_ready_set;
uint64_t blkdev_ready_set[MAX_BLK_DEVICES];

static uint64_t netdev_to_setid(net_id_t id)
{
    assert(id < 63);
    return 1L << id;
}

static uint64_t token_to_setid(token_id_t id)
{
    assert(id < 63);
    return 1L << id;
}

void set_netdev_queue_ready(net_id_t id)
{
    pthread_mutex_lock(&ready_sets_mutex);
    netdev_ready_set |= netdev_to_setid(id);
    pthread_mutex_unlock(&ready_sets_mutex);
}

void set_netdev_queue_empty(net_id_t id)
{
    pthread_mutex_lock(&ready_sets_mutex);
    netdev_ready_set &= ~(netdev_to_setid(id));
    pthread_mutex_unlock(&ready_sets_mutex);
}

static bool netdev_is_queue_ready(net_id_t id)
{
    bool ready;

    pthread_mutex_lock(&ready_sets_mutex);
    ready = (netdev_ready_set & netdev_to_setid(id)) != 0;
    pthread_mutex_unlock(&ready_sets_mutex);
    return ready;
}

void signal_netdev_queue_ready(net_id_t id)
{
    pthread_mutex_lock(&ready_sets_mutex);
    netdev_ready_set |= netdev_to_setid(id);
    pthread_cond_broadcast(&ready_sets_cond);
    pthread_mutex_unlock(&ready_sets_mutex);
}

void signal_block_request_ready(block_id_t devid, token_id_t tokenid)
{
    pthread_mutex_lock(&ready_sets_mutex);
    blkdev_ready_set[devid] |= token_to_setid(tokenid);
    pthread_cond_broadcast(&ready_sets_cond);
    pthread_mutex_unlock(&ready_sets_mutex);
}

void set_block_request_completed(block_id_t devid, token_id_t tokenid)
{
    pthread_mutex_lock(&ready_sets_mutex);
    blkdev_ready_set[devid] &= ~(token_to_setid(tokenid));
    pthread_mutex_unlock(&ready_sets_mutex);
}

typedef struct {
    net_id_t netid;
    block_id_t blkid;
    token_id_t tokid;
} t_next_io;

#define NANO 1000000000
static bool yield(uint64_t deadline, t_next_io *next_io)
{
    struct timeval now;
    struct timespec timeout;
    bool ready = false;

    gettimeofday(&now, NULL);
    timeout.tv_sec = now.tv_sec  + deadline / NANO;
    timeout.tv_nsec = now.tv_usec * 1000 + deadline % NANO;
    if (timeout.tv_nsec >= NANO) {
        timeout.tv_sec += 1;
        timeout.tv_nsec -= NANO;
    }

    pthread_mutex_lock(&ready_sets_mutex);
    do {
        if (netdev_ready_set != 0) {
          for (net_id_t i = 0; i < MAX_NET_DEVICES; i++) {
              if (netdev_ready_set & netdev_to_setid(i)) {
                  ready = true;
                  next_io->netid = i;
                  goto out;
              }
          }
        }
        for (block_id_t i = 0; i < MAX_BLK_DEVICES; i++) {
            if (blkdev_ready_set[i] != 0) {
                for (token_id_t j = 0; j < MAX_BLK_TOKENS; j++) {
                    if (blkdev_ready_set[i] & token_to_setid(j)) {
                        ready = true;
                        next_io->blkid = i;
                        next_io->tokid = j;
                        goto out;
                    }
                }
            }
        }

        int rc = pthread_cond_timedwait(&ready_sets_cond, &ready_sets_mutex,
                &timeout);
        if (rc == ETIMEDOUT) {
            break;
        }
    } while (1);
out:
    pthread_mutex_unlock(&ready_sets_mutex);
    return ready;
}

value uk_yield(value v_deadline)
{
    CAMLparam1(v_deadline);
    CAMLlocal1(v_result);

    const int64_t deadline = Int64_val(v_deadline);
    assert(deadline >= 0);

    t_next_io next_io = {-1, -1, -1};
    const bool result = yield(deadline, &next_io);

    if (result) {
        if (next_io.netid != -1) {
            v_result = caml_alloc(1, 0); // key:Net
            Store_field(v_result, 0, Val_int(next_io.netid));
        }
        else {
            assert(next_io.blkid != -1);
            assert(next_io.tokid != -1);
            v_result = caml_alloc(2, 1); // key:Block
            Store_field(v_result, 0, Val_int(next_io.blkid));
            Store_field(v_result, 1, Val_int(next_io.tokid));
        }
        assert(v_result != Val_int(0));
    }
    else {
        v_result = Val_int(0); // key:Nothing
    }

    CAMLreturn(v_result);
}

value uk_netdev_is_queue_ready(value v_devid)
{
    CAMLparam1(v_devid);
    
    long id = Long_val(v_devid);
    if (netdev_is_queue_ready(id)) {
        CAMLreturn(Val_true);
    }
    CAMLreturn(Val_false);
}

#endif /* __Unikraft__ */
