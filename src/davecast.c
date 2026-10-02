#include <davecast.h>
#include <stdlib.h>
#include <useful.h>
#include <ws.h>
#include <identify.h>
#include <event.h>

#include <cjson/cJSON.h>

struct dcast_session* dcast_connect(const dcast_config *cfg, dcast_event_cb cb, void *userdata) {
    struct dcast_session* session = malloc(sizeof(dcast_session)); 
    if (!session) return NULL;
    session->cfg = cfg;
    
    struct dcast_socket* gway_sock = dcast_ws_connect("wss://gateway.discord.gg/?v=10&encoding=json", "gateway");
    if (!gway_sock) return NULL;
    session->gateway_wsocket = gway_sock;

    unsigned char identified = 0;
    for (;;) {
        int gw_pump_res = dcast_ws_pump(gway_sock);
        if (gw_pump_res < 0) return NULL; /* connection closed */
        if (gw_pump_res != 1) continue; // 1 = ws got something

        cJSON* json = cJSON_Parse(gway_sock->buf);
        if (!json) {
            cJSON_Delete(json);
            return NULL;
        }

        cJSON *seq = cJSON_GetObjectItem(json, "s");
        if (seq) {
            session->seq = seq->valueint;
        }

        cJSON *op = cJSON_GetObjectItem(json, "op");
        switch (op->valueint) { 
            case 10: {
                cJSON *d = cJSON_GetObjectItem(json, "d");
                if (d) {
                    cJSON *heartbeat = cJSON_GetObjectItem(d, "heartbeat_interval");
                    gway_sock->hb_ms = heartbeat->valueint;

                    dcast_identify(gway_sock, cfg); /* identify ourselves -> identify.c */

                    gway_sock->next_hb = dch_now_ms() + gway_sock->hb_ms / 2;
                    identified = 1;
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
        }

        cJSON_Delete(json); // free
    }

    return session;
}