#ifndef IDENTIFY_H
#define IDENTIFY_H

#include <davecast.h>
#include <ws.h>

void dcast_identify(struct dcast_socket* wsock, const struct dcast_config* cfg);

#endif