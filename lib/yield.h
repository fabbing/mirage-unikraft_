#ifndef YIELD_H
#define YIELD_H

#include <stdint.h>

#define MAX_NET_DEVICES   16
#define MAX_BLK_DEVICES   16
#define MAX_BLK_TOKENS    32

void signal_netdev_queue_ready(int64_t id);

void set_netdev_queue_ready(uint64_t id);
void set_netdev_queue_empty(uint64_t id);

void signal_block_request_ready(unsigned int devid, unsigned int tokenid);
void set_block_request_completed(unsigned int devid, unsigned int tokenid);

#endif /* !YIELD_H */
