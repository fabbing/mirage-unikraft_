#ifdef __Unikraft__

#include <assert.h>
#include <pthread.h>

#include <caml/alloc.h>
#include <caml/callback.h>
#include <caml/memory.h>
#include <caml/mlvalues.h>

void *uk_caml_main(void *argv) {
    caml_startup((char**) argv);
    return NULL;
}

int main(int argc, char **argv) {
    pthread_t m;
    assert(pthread_create(&m, NULL, &uk_caml_main, argv) == 0);
    assert(pthread_join(m, NULL) == 0);
    return 0;
}

int64_t uk_clock_monotonic(void)
{
    struct timespec ts;

    assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

CAMLprim value caml_get_monotonic_time(value v_unit)
{
    CAMLparam1(v_unit);
    CAMLreturn(caml_copy_int64(uk_clock_monotonic()));
}

#endif
