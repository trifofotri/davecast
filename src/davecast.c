#include <davecast.h>
#include <stdlib.h>
#include <useful.h>
#include <ws.h>
#include <identify.h>
#include <event.h>
#include <dcast_dave.h>

#include <cjson/cJSON.h>

struct dcast_session* dcast_connect(const dcast_config *cfg) {
    struct dcast_session* session = malloc(sizeof(dcast_session)); 
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

    int gw_pump_res = dcast_ws_pump(gway_sock);
    if (gw_pump_res < 0) return 1; /* connection closed */
    if (gw_pump_res != 1) return 0; // gw_pump_res = 1 means ws got something

    cJSON* json = cJSON_Parse(gway_sock->buf);
    if (!json) {
        cJSON_Delete(json);
        return 1;
    }

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
            printf("[POLL] op: 7, gateway wants reconnect.\n");
            break;
        }

        case 9: {
            printf("[POLL] op: 9, INVALID SESSION, bad token or something.\n");
            break;
        }
    }

    if (session->media_wsocket) {
        int mw_pump_res = dcast_ws_pump(session->media_wsocket);
        if (mw_pump_res != 1) return 0;

        dcast_dave_on_binary(session, (const unsigned char *) session->media_wsocket->buf, session->media_wsocket->mlen);
    }

    cJSON_Delete(json); // free
    return 0;
}