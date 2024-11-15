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

pthread_mutex_t ready_sets_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ready_sets_cond = PTHREAD_COND_INITIALIZER;
bool ready_flag = false;
uint64_t netdev_ready_set;

int64_t netdev_to_setid(uint64_t id)
{
    return 1 << id;
}

void set_netdev_queue_ready(uint64_t id)
{
    pthread_mutex_lock(&ready_sets_mutex);
    netdev_ready_set |= netdev_to_setid(id);
    ready_flag = true;
    pthread_mutex_unlock(&ready_sets_mutex);
}

void set_netdev_queue_empty(uint64_t id)
{
    pthread_mutex_lock(&ready_sets_mutex);
    netdev_ready_set &= ~(netdev_to_setid(id));
    if (netdev_ready_set == 0) {
        ready_flag = false;
    }
    pthread_mutex_unlock(&ready_sets_mutex);
}

static bool netdev_is_queue_ready(int64_t id)
{
    bool ready;

    pthread_mutex_lock(&ready_sets_mutex);
    ready = (netdev_ready_set & netdev_to_setid(id)) != 0;
    pthread_mutex_unlock(&ready_sets_mutex);
    return ready;
}


void signal_netdev_queue_ready(uint64_t id)
{
    pthread_mutex_lock(&ready_sets_mutex);
    netdev_ready_set |= netdev_to_setid(id);
    ready_flag = true;
    pthread_cond_broadcast(&ready_sets_cond);
    pthread_mutex_unlock(&ready_sets_mutex);
}

#define NANO 1000000000
static void yield(uint64_t deadline, int64_t *ready_set)
{
    struct timeval now;
    struct timespec timeout;
    int rc = 0;

    *ready_set = 0;

    gettimeofday(&now, NULL);
    timeout.tv_sec = now.tv_sec  + deadline / NANO;
    timeout.tv_nsec = now.tv_usec * 1000 + deadline % NANO;
    if (timeout.tv_nsec >= NANO) {
        timeout.tv_sec += 1;
        timeout.tv_nsec -= NANO;
    }

    pthread_mutex_lock(&ready_sets_mutex);
    while (!ready_flag) {
        rc = pthread_cond_timedwait(&ready_sets_cond, &ready_sets_mutex,
            &timeout);
        if (rc != EINTR)
            break;
    }

    if (ready_flag) {
        *ready_set = netdev_ready_set;
    }
    else {
        assert(netdev_ready_set == 0);
    }
    pthread_mutex_unlock(&ready_sets_mutex);
}

CAMLprim value uk_yield(value v_deadline)
{
    CAMLparam1(v_deadline);

    uint64_t deadline = Int64_val(v_deadline);
    uint64_t ready_set;
    yield(deadline, &ready_set);

    CAMLreturn(caml_copy_int64(ready_set));
}


CAMLprim value uk_netdev_is_queue_ready(value v_devid)
{
    CAMLparam1(v_devid);
    
    int64_t id = Int64_val(v_devid);
    if (netdev_is_queue_ready(id)) {
        CAMLreturn(Val_true);
    }
    CAMLreturn(Val_false);
}
