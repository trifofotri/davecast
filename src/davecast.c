#include <dcast_media.h>
#include <davecast.h>
#include <stdlib.h>
#include <useful.h>
#include <ws.h>
#include <identify.h>
#include <event.h>
#include <dcast_dave.h>
#include <dcast_rtp.h>

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
    if (session->identified && gway_sock->hb_ms && now >= gway_sock->next_hb) {
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

    int gw_pump_res = dcast_ws_pump(gway_sock);
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
            int pt = rtcp[1];
            DCAST_DEBUG("[udp]: RTCP type: %d len: %zd\n", pt, n);
            if (pt == 206) {
                session->pli_pending = 1;
                DCAST_DEBUG("[udp] [PLI/FIR] requested\n");
            }
        }
    }
    return 0;
}

int dcast_send_video(dcast_session *s, const void *annexb, size_t len, uint64_t pts_us, int keyframe) {
    return dcast_rtp_send_video(s, annexb, len, pts_us, keyframe);
}