#include <dcast_media.h>
#include <davecast.h>
#include <stdlib.h>
#include <useful.h>
#include <ws.h>
#include <identify.h>
#include <event.h>
#include <dcast_dave.h>
#include <dcast_rtp.h>
#include <streamandvoice.h>

#include <cjson/cJSON.h>

struct dcast_session* dcast_connect(const dcast_config *cfg) {
    struct dcast_session* session = calloc(1, sizeof(dcast_session)); 
    if (!session) return NULL;
    session->cfg = cfg;

    struct dcast_socket* gway_sock = dcast_ws_connect("wss://gateway.discord.gg/?v=10&encoding=json", "gateway");
    if (!gway_sock) {
        free(session);
        return NULL;
    }

    session->gateway_wsocket = gway_sock;

    return session;
}

int dcast_poll(struct dcast_session* session, int timeout) {
    struct dcast_socket* gway_sock = session->gateway_wsocket;
    
    unsigned long long now = dch_now_ms();

    // heartbeats uh
    if (gway_sock && session->identified && gway_sock->hb_ms && now >= gway_sock->next_hb) {
        cJSON* payload = cJSON_CreateObject();
        cJSON_AddNumberToObject(payload, "op", 1);
        if (session->seq > 0) cJSON_AddNumberToObject(payload, "d", session->seq);
        else cJSON_AddNullToObject(payload, "d");
        dcast_ws_send(gway_sock, payload);
        gway_sock->next_hb = now + gway_sock->hb_ms;
    }
    if (session->media_wsocket && session->media_wsocket->hb_ms && now >= session->media_wsocket->next_hb) {
        cJSON* d = cJSON_CreateObject();
        cJSON_AddNumberToObject(d, "t", (double)now);
        cJSON_AddNumberToObject(d, "seq_ack", session->media_seq);
        cJSON* o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "op", 3);
        cJSON_AddItemToObject(o, "d", d);
        dcast_ws_send(session->media_wsocket, o);
        session->media_wsocket->next_hb = now + session->media_wsocket->hb_ms;
    }

    int gw_pump_res = gway_sock ? dcast_ws_pump(gway_sock) : 0;
    if (gw_pump_res < 0) return 1; /* connection closed */
    
    if (gw_pump_res == 1) {
        cJSON* json = cJSON_Parse(gway_sock->buf);
        if (json) { // gw_pump_res = 1 means ws got something
            cJSON *seq = cJSON_GetObjectItem(json, "s");
            if (seq) {
                session->seq = seq->valueint;
            }

            cJSON *op = cJSON_GetObjectItem(json, "op");
            if (!cJSON_IsNumber(op)) {
                cJSON_Delete(json);
                return 1;
            }

            switch (op->valueint) {
                case 10: {
                    cJSON *d = cJSON_GetObjectItem(json, "d");
                    if (d) {
                        cJSON *heartbeat = cJSON_GetObjectItem(d, "heartbeat_interval");
                        if (!cJSON_IsNumber(heartbeat)) return 1;
                        gway_sock->hb_ms = heartbeat->valueint;

                        dcast_identify(gway_sock, session->cfg); /* identify ourselves -> identify.c */

                        gway_sock->next_hb = dch_now_ms() + gway_sock->hb_ms / 2;
                        session->identified = 1;
                    }
                    break;
                }
                
                /* dispatch event */
                case 0: {
                    dcast_dispatch_event(session, json);
                    break;
                }

                /* heartbeat event */
                case 1: {
                    cJSON* payload = cJSON_CreateObject();
                    cJSON_AddNumberToObject(payload, "op", 1);
                    if (session->seq > 0) {
                        cJSON_AddNumberToObject(payload, "d", session->seq);
                    } else { 
                        cJSON_AddNullToObject(payload, "d");
                    }
                    dcast_ws_send(gway_sock, payload);

                    break;
                }

                case 7: {
                    DCAST_DEBUG("[POLL] op: 7, gateway wants reconnect.\n");
                    break;
                }

                case 9: {
                    DCAST_DEBUG("[POLL] op: 9, INVALID SESSION, bad token or something.\n");
                    break;
                }
            }
            cJSON_Delete(json); // free
        } 
    }  

    if (session->media_wsocket) {
        int mw_pump_res = dcast_ws_pump(session->media_wsocket);
        if (mw_pump_res < 0) return 1;
        if (mw_pump_res == 1) {
            if (session->media_wsocket->buf[0] == '{') {
                cJSON* media_json = cJSON_Parse(session->media_wsocket->buf);
                if (media_json) {
                    dcast_on_media(session, media_json);
                    cJSON_Delete(media_json);
                }
            } else {
                dcast_dave_on_binary(session, (const unsigned char *) session->media_wsocket->buf, session->media_wsocket->mlen);
            }
        }
    }
    
    if (session->udp_fd >= 0) {
        unsigned char rtcp[2048];
        ssize_t n = recv(session->udp_fd, rtcp, sizeof rtcp, 0);
        
        if (n >= 2) {
            DCAST_DEBUG("[udp]: RTCP type: %d len: %zd\n", pt, n);
            int fmt = rtcp[0] & 0x1F, pt = rtcp[1];
            if (pt == 206 && (fmt == 1 || fmt == 4)) {
                session->pli_pending = 1;
            }
        }
    }
    return 0;
}

int dcast_send_video(dcast_session *s, const void *annexb, size_t len, uint64_t pts_us, int keyframe) {
    return dcast_rtp_send_video(s, annexb, len, pts_us, keyframe);
}

int dcast_send_audio(dcast_session *s, const void *opus, size_t len, uint64_t pts_us) {
    return dcast_rtp_send_audio(s, opus, len, pts_us);
}

dcast_session *dcast_new(const dcast_config *cfg) {
    dcast_session *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->cfg = cfg; s->udp_fd = -1;
    return s;
}

//idk
static void cp(char *dst, size_t n, cJSON *j, const char *k) {
    const char *v = cJSON_GetStringValue(cJSON_GetObjectItem(j, k));
    snprintf(dst, n, "%s", v ? v : "");
}

int dcast_inject(dcast_session *s, const char *json) {
    cJSON *j = cJSON_Parse(json);
    if (!j) return -1;

    cp(s->user_id,sizeof s->user_id, j, "user_id");
    cp(s->session_id,sizeof s->session_id,j, "session_id");
    cp(s->rtc_server,sizeof s->rtc_server,j, "rtc_server");
    cp(s->rtc_channel,sizeof s->rtc_channel,j,"rtc_channel");
    cp(s->stream_key,sizeof s->stream_key,j, "stream_key");
    cp(s->supdate_endpoint, sizeof s->supdate_endpoint, j, "endpoint");
    cp(s->supdate_token, sizeof s->supdate_token, j, "token");
    cJSON_Delete(j);
    s->havesupdate = 1;
    s->stream_requested = 1;
    dcast_dave_ensure_session(s);   /* no-op until ext sender arrives */
    dcast_start_media(s);
    return s->media_wsocket ? 0 : -1;
}
