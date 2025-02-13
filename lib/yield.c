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


uint64_t netdev_to_setid(long id)
{
    assert(id < 63);
    return 1 << id;
}

uint64_t token_to_setid(long id)
{
    assert(id < 63);
    return 1 << id;
}

void set_netdev_queue_ready(uint64_t id)
{
    pthread_mutex_lock(&ready_sets_mutex);
    netdev_ready_set |= netdev_to_setid(id);
    pthread_mutex_unlock(&ready_sets_mutex);
}

void set_netdev_queue_empty(uint64_t id)
{
    pthread_mutex_lock(&ready_sets_mutex);
    netdev_ready_set &= ~(netdev_to_setid(id));
    pthread_mutex_unlock(&ready_sets_mutex);
}

static bool netdev_is_queue_ready(long id)
{
    bool ready;

    pthread_mutex_lock(&ready_sets_mutex);
    ready = (netdev_ready_set & netdev_to_setid(id)) != 0;
    pthread_mutex_unlock(&ready_sets_mutex);
    return ready;
}

void signal_netdev_queue_ready(long id)
{
    pthread_mutex_lock(&ready_sets_mutex);
    netdev_ready_set |= netdev_to_setid(id);
    pthread_cond_broadcast(&ready_sets_cond);
    pthread_mutex_unlock(&ready_sets_mutex);
}

void signal_block_request_ready(unsigned int devid, unsigned int tokenid)
{
    pthread_mutex_lock(&ready_sets_mutex);
    blkdev_ready_set[devid] |= token_to_setid(tokenid);
    pthread_cond_broadcast(&ready_sets_cond);
    pthread_mutex_unlock(&ready_sets_mutex);
}

void set_block_request_completed(unsigned int devid, unsigned int tokenid)
{
    pthread_mutex_lock(&ready_sets_mutex);
    blkdev_ready_set[devid] &= ~(token_to_setid(tokenid));
    pthread_mutex_unlock(&ready_sets_mutex);
}

#define NANO 1000000000
static bool yield(uint64_t deadline)
{
    struct timeval now;
    struct timespec timeout;
    int rc = 0;

    gettimeofday(&now, NULL);
    timeout.tv_sec = now.tv_sec  + deadline / NANO;
    timeout.tv_nsec = now.tv_usec * 1000 + deadline % NANO;
    if (timeout.tv_nsec >= NANO) {
        timeout.tv_sec += 1;
        timeout.tv_nsec -= NANO;
    }

    pthread_mutex_lock(&ready_sets_mutex);
    bool ready = netdev_ready_set != 0;
    for (int i = 0; i < MAX_BLK_DEVICES && !ready; i++) {
        ready = blkdev_ready_set[i] != 0;
    }

    while (!ready) {
        rc = pthread_cond_timedwait(&ready_sets_cond, &ready_sets_mutex,
            &timeout);
        if (rc == ETIMEDOUT) {
            break;
        }

        ready = netdev_ready_set != 0;
        for (int i = 0; i < MAX_BLK_DEVICES && !ready; i++) {
            ready = blkdev_ready_set[i] != 0;
        }
    }
    pthread_mutex_unlock(&ready_sets_mutex);
    return ready;
}

value uk_yield(value v_deadline)
{
    CAMLparam1(v_deadline);

    int64_t deadline = Int64_val(v_deadline);
    assert(deadline >= 0);

    bool result = yield(deadline);

    CAMLreturn(result ? Val_true : Val_false);
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

value uk_next_io(value v_unit)
{
    CAMLparam1(v_unit);
    CAMLlocal1(v_result);

    v_result = Val_int(0); // key:Nothing

    pthread_mutex_lock(&ready_sets_mutex);
    if (netdev_ready_set != 0) {
        for (int i = 0; i < MAX_NET_DEVICES; i++) {
            if (netdev_ready_set & netdev_to_setid(i)) {
                v_result = caml_alloc(1, 0); /* key:Net */
                Store_field(v_result, 0, Val_int(i));
                break;
                //CAMLreturn(v_result);
            }
        }
        assert(v_result != Val_int(0));
    }
    else {
        for (int i = 0; i < MAX_BLK_DEVICES; i++) {
            if (blkdev_ready_set[i] != 0) {
                for (int j = 0; j < MAX_BLK_TOKENS; j++) {
                    if (blkdev_ready_set[i] & token_to_setid(j)) {
                        v_result = caml_alloc(2, 1);
                        Store_field(v_result, 0, Val_int(i));
                        Store_field(v_result, 1, Val_int(j));
                        break;
                    }
                }
                assert(v_result != Val_int(0));
            }
        }
    }
    pthread_mutex_unlock(&ready_sets_mutex);
    CAMLreturn(v_result);
}
