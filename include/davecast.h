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
    const dcast_config* cfg;
    int seq;
    char user_id[32];
    char session_id[128];
    int stream_requested;
    char stream_key[192];
} dcast_session;

typedef enum {
    DCAST_EV_READY,           /* gateway READY: user_id / session_id       */
    DCAST_EV_VOICE_JOINED,    /* our VOICE_STATE_UPDATE confirmed          */
    DCAST_EV_STREAM_CREATED,  /* rtc_server_id / rtc_channel_id / key      */
    DCAST_EV_MEDIA_CONNECTING,
    DCAST_EV_DAVE_HANDSHAKE,
    DCAST_EV_DAVE_ESTABLISHED,/* E2EE up: epoch_authenticator valid        */
    DCAST_EV_LIVE,            /* frames accepted from now on               */
    DCAST_EV_VIEWER_JOIN,     /* op 11 (also drives the DAVE member list)  */
    DCAST_EV_VIEWER_LEAVE,    /* op 13                                     */
    DCAST_EV_KEYFRAME_NEEDED, /* PLI/NACK: you MUST push an IDR in ~100 ms */
    DCAST_EV_STATS,
    DCAST_EV_WARN,            /* recoverable: unknown op, stale build...   */
    DCAST_EV_ERROR,           /* fatal; session is heading to CLOSED       */
    DCAST_EV_CLOSED,          /* d.closed.code; 4017 = server wants DAVE   */
} dcast_event_type;

typedef struct dcast_event {
    dcast_event_type type;
    union {
        struct { const char *user_id, *session_id; } ready;
        struct { const char *rtc_server_id, *rtc_channel_id, *stream_key; } stream;
        struct { int version; const uint8_t *auth; size_t auth_len; } dave;
        struct { const char *user_id; } viewer;
        struct { double rtt_ms, loss_pct, send_kbps; int viewers, nacks; } stats;
        struct { const char *msg; } warn, error;
        struct { int code; const char *reason; } closed;
    } d;
} dcast_event;

typedef void (*dcast_event_cb)(const dcast_event *ev, void *userdata);

dcast_session *dcast_connect(const dcast_config *cfg, dcast_event_cb cb, void *userdata);

int dcast_send_video(dcast_session *s, const void *annexb, size_t len, uint64_t pts_us, int keyframe);

#endif