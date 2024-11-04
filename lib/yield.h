#ifndef YIELD_H
#define YIELD_H

#include <stdint.h>

void signal_netdev_queue_ready(int64_t id);

void set_netdev_queue_ready(uint64_t id);
void set_netdev_queue_empty(uint64_t id);

#endif /* !YIELD_H */
