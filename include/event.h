// https://docs.discord.com/developers/topics/opcodes-and-status-codes
#ifndef DCAST_EVENT_H
#define DCAST_EVENT_H

#include <cjson/cJSON.h>
#include <davecast.h>

void dcast_dispatch_event(struct dcast_session* session, cJSON* json_obj);

#endif