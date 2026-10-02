#ifndef DAVECAST_H
#define DAVECAST_H

#include "ws.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
    DCAST_DAVE_AUTO = 0,
    DCAST_DAVE_OFF,       /* announce 0 (dies with 4017 if enforced)     */
    DCAST_DAVE_REQUIRE,
} dcast_dave_mode;

/* what op 12 will declare */
typedef struct dcast_video {
    int width, height, framerate, kbps;
} dcast_video;

typedef struct dcast_config {
    const char* token;
    const char* guild_id;
    const char* channel_id;

    const char* client_version;
    int client_build;
    
    const char* user_agent;
    unsigned capabilities;

    int seq;

    dcast_dave_mode dave;
    dcast_video video;
} dcast_config;

typedef struct dcast_session {
    dcast_socket* gateway_wsocket;
    dcast_socket* media_wsocket;
    const dcast_config* cfg;
    int seq;
    int media_seq;
    char user_id[32];
    char session_id[128];
    int stream_requested;
    char stream_key[192];
    int identified;
    char rtc_channel[32];
    char rtc_server[32];
    unsigned a_ssrc, v_ssrc, r_ssrc; // dave bs
    char supdate_token[128];
    char supdate_endpoint[256];
    int havesupdate;
    char media_ip[64];
    int media_port;
    int udp_fd;
    char pub_ip[72];
    int pub_port;
} dcast_session;

dcast_session *dcast_connect(const dcast_config *cfg);

int dcast_poll(struct dcast_session* session, int timeout_ms);
int dcast_send_video(dcast_session *s, const void *annexb, size_t len, uint64_t pts_us, int keyframe);

#endif