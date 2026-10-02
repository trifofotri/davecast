#ifndef DCAST_MEDIA_H
#define DCAST_MEDIA_H

#include <davecast.h>
#include <cjson/cJSON.h>
#include <arpa/inet.h>

void dcast_on_media(struct dcast_session* session, cJSON* json_obj);

extern struct sockaddr_in media_addr; 

#endif