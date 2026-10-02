#ifndef DWS_H
#define DWS_H

#include <fcntl.h>
#include <cjson/cJSON.h>
#include <curl/curl.h>

#define BUFCAP ( 64u << 20 ) // 64 mib

typedef struct dcast_socket {
    CURL *c;
    curl_socket_t fd;
    char *buf;
    size_t len, mlen;
    int hb_ms;
    unsigned long long next_hb;
    const char *name;
} dcast_socket;

struct dcast_socket* dcast_ws_connect(const char *url, const char *name);

// 1 = complete message in w->buf (length w->mlen)
// 2 = got bytes / control frame, keep calling
// 0 = nothing more right now (EAGAIN)
// -1 = connection dead
int dcast_ws_pump(dcast_socket* wsock);

void dcast_ws_send(dcast_socket* wsock, cJSON* json_obj);

#endif 