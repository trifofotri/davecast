#ifndef DCAST_USEFUL_H
#define DCAST_USEFUL_H

#include <davecast.h>

unsigned long long dch_now_ms(void);
void dch_udp_discover(struct dcast_session* session);

#endif