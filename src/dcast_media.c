#include "cjson/cJSON.h"
#include "davecast.h"
#include <dcast_dave.h>
#include <identify.h>
#include <stdlib.h>
#include <useful.h>
#include <dcast_media.h>

void dcast_mediaop12(struct dcast_session* session, int active) {
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "type", "fixed");
    cJSON_AddNumberToObject(res, "width", session->cfg->video.width);
    cJSON_AddNumberToObject(res, "height", session->cfg->video.height);

    cJSON *st = cJSON_CreateObject();
    cJSON_AddStringToObject(st, "type", "video");
    cJSON_AddStringToObject(st, "rid", "100");
    cJSON_AddNumberToObject(st, "ssrc", session->v_ssrc);
    cJSON_AddBoolToObject(st, "active", active);
    cJSON_AddNumberToObject(st, "quality", 100);
    cJSON_AddNumberToObject(st, "rtx_ssrc", session->r_ssrc);
    cJSON_AddNumberToObject(st, "max_bitrate", 8000000);
    cJSON_AddNumberToObject(st, "max_framerate", 60);
    cJSON_AddItemToObject(st, "max_resolution", res);

    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, st);

    cJSON *d = cJSON_CreateObject();
    cJSON_AddNumberToObject(d, "audio_ssrc", session->a_ssrc);
    cJSON_AddNumberToObject(d, "video_ssrc", session->v_ssrc);
    cJSON_AddNumberToObject(d, "rtx_ssrc", session->r_ssrc);
    cJSON_AddItemToObject(d, "streams", arr);

    cJSON *o12 = cJSON_CreateObject();
    cJSON_AddNumberToObject(o12, "op", 12);
    cJSON_AddItemToObject(o12, "d", d);

    dcast_ws_send(session->media_wsocket, o12);
}

void dcast_select_protocol(struct dcast_session* session) {
    cJSON *opus = cJSON_CreateObject();
    cJSON_AddStringToObject(opus, "name", "opus");
    cJSON_AddStringToObject(opus, "type", "audio");
    cJSON_AddNumberToObject(opus, "priority", 1000);
    cJSON_AddNumberToObject(opus, "payload_type", 120);

    cJSON *h264 = cJSON_CreateObject();
    cJSON_AddStringToObject(h264, "name", "H264");
    cJSON_AddStringToObject(h264, "type", "video");
    cJSON_AddNumberToObject(h264, "priority", 2000);
    cJSON_AddNumberToObject(h264, "payload_type", 103);
    cJSON_AddNumberToObject(h264, "rtx_payload_type", 104);
    cJSON_AddBoolToObject(h264, "encode", 1);
    cJSON_AddBoolToObject(h264, "decode", 1);

    cJSON *codecs = cJSON_CreateArray();
    cJSON_AddItemToArray(codecs, opus);
    cJSON_AddItemToArray(codecs, h264);

    unsigned char rb[16];
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(rb, 1, 16, f) != 16) return;
    fclose(f);

    char uuid[40];
    snprintf(uuid, sizeof uuid, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", rb[0], rb[1], rb[2], rb[3], rb[4], rb[5], rb[6], rb[7], rb[8], rb[9], rb[10], rb[11], rb[12], rb[13], rb[14], rb[15]);

    cJSON *dd = cJSON_CreateObject();
    cJSON_AddStringToObject(dd, "address", session->pub_ip);
    cJSON_AddNumberToObject(dd, "port", session->pub_port);
    cJSON_AddStringToObject(dd, "mode", "aead_aes256_gcm_rtpsize");

    cJSON *exps = cJSON_CreateArray();
    cJSON_AddItemToArray(exps, cJSON_CreateString("fixed_keyframe_interval"));

    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "protocol", "udp");
    cJSON_AddItemToObject(d, "data", dd);
    cJSON_AddStringToObject(d, "address", session->pub_ip);
    cJSON_AddNumberToObject(d, "port", session->pub_port);
    cJSON_AddStringToObject(d, "mode", "aead_aes256_gcm_rtpsize");
    cJSON_AddItemToObject(d, "codecs", codecs);
    cJSON_AddStringToObject(d, "rtc_connection_id", uuid);
    cJSON_AddItemToObject(d, "experiments", exps);

    cJSON *o1 = cJSON_CreateObject();
    cJSON_AddNumberToObject(o1, "op", 1);
    cJSON_AddItemToObject(o1, "d", d);

    dcast_ws_send(session->media_wsocket, o1);
}

void dcast_on_media(struct dcast_session* session, cJSON* json_obj) {
    cJSON* op = cJSON_GetObjectItem(json_obj, "op");
    if (!cJSON_IsNumber(op)) return;

    cJSON* seq = cJSON_GetObjectItem(json_obj, "seq");
    if (cJSON_IsNumber(seq) && seq->valueint >= 0) session->media_seq = seq->valueint;

    cJSON* m_data = cJSON_GetObjectItem(json_obj, "d");
    switch (op->valueint) {
        /* heartbeat opcode */
        case 8: {
            cJSON *heartbeat = cJSON_GetObjectItem(m_data, "heartbeat_interval");
            if (!cJSON_IsNumber(heartbeat)) return;
            session->media_wsocket->hb_ms = heartbeat->valueint;
            session->media_wsocket->next_hb = dch_now_ms() + session->media_wsocket->hb_ms / 2;
            printf("[media]: HELLO hb: %d ms\n", session->media_wsocket->hb_ms);
            break;
        }

        case 2: {
            cJSON *ssrc = cJSON_GetObjectItem(m_data, "ssrc");
            if (!cJSON_IsNumber(ssrc)) return;
            session->a_ssrc = ssrc->valueint;

            cJSON *port = cJSON_GetObjectItem(m_data, "port");
            if (!cJSON_IsNumber(port)) return;
            session->media_port = port->valueint;

            cJSON *ip = cJSON_GetObjectItem(m_data, "ip");
            char* ip_string = cJSON_GetStringValue(ip);
            snprintf(session->media_ip,  sizeof(session->media_ip),  "%s", ip_string);

            cJSON *streams_arr = cJSON_GetObjectItem(m_data, "streams");
            if (streams_arr && cJSON_GetArraySize(streams_arr) > 0) {
                cJSON *s0 = cJSON_GetArrayItem(streams_arr, 0);

                session->v_ssrc = (unsigned) cJSON_GetObjectItem(s0, "ssrc")->valueint;
                session->r_ssrc = (unsigned) cJSON_GetObjectItem(s0, "rtx_ssrc")->valueint;
            }

            printf("[media] [READY]: audio_ssrc: %u video_ssrc: %u rtx_ssrc: %u udp:%s:%d\n", session->a_ssrc, session->v_ssrc, session->r_ssrc, session->media_ip, session->media_port);
            
            {   /* version ping */
                cJSON *o16 = cJSON_CreateObject();
                cJSON_AddNumberToObject(o16, "op", 16);
                cJSON_AddItemToObject(o16, "d", cJSON_CreateObject());
                dcast_ws_send(session->media_wsocket, o16);
            }
            dcast_mediaop12(session, 0); // declare stream, inactive
            dch_udp_discover(session);
            dcast_select_protocol(session);

            cJSON *sp = cJSON_CreateObject();
            cJSON_AddNumberToObject(sp, "speaking", 2);
            cJSON_AddNumberToObject(sp, "delay", 0);
            cJSON_AddNumberToObject(sp, "ssrc", session->a_ssrc);

            { 
                cJSON *o5 = cJSON_CreateObject();
                cJSON_AddNumberToObject(o5, "op", 5);
                cJSON_AddItemToObject(o5, "d", sp);
                dcast_ws_send(session->media_wsocket, o5);
            }

            dcast_mediaop12(session, 1); // finally go live.
            break;
        }

        case 4: { /* SESSION_DESCRIPTION */
            cJSON *secret_key = cJSON_GetObjectItem(m_data, "secret_key");
            int n = secret_key ? cJSON_GetArraySize(secret_key) : 0;

            if (n == 32) {
                for (int i = 0; i < 32; i++) {
                    session->key[i] = (unsigned char) cJSON_GetArrayItem(secret_key, i)->valueint;
                }
                session->have_key = 1;
            }

            printf("[media] [SESSION_DESCRIPTION] mode: %s video: %s dave: %d\n", cJSON_GetObjectItem(m_data, "mode")->valuestring, cJSON_GetObjectItem(m_data, "video_codec")->valuestring, cJSON_GetObjectItem(m_data, "dave_protocol_version")->valueint);
            
            {
                int dv = cJSON_GetObjectItem(m_data, "dave_protocol_version")->valueint;

                session->dave_active = (dv > 0);
                if (dv > 0) {
                    dcast_dave_state.version = (uint16_t) dv;
                    dcast_dave_ensure_session(session);
                }
            }

            if (session->have_key) {
                char hx[65];

                for (int i = 0; i < 32; i++) {
                    sprintf(hx + 2 * i, "%02x", session->key[i]);
                }
                printf("key_hex: %s\n key_csv:", hx);

                for (int i = 0; i < 32; i++) {
                    printf(i ? ",%u" : "%u", session->key[i]);
                }
                printf("\n");

                session->live = 1;

                printf("[media] stream session established.\n");
            }

            break;
        }

        case 11: {
            cJSON *ids = cJSON_GetObjectItem(m_data, "user_ids");
            int n = ids ? cJSON_GetArraySize(ids) : 0;
            for (int i = 0; i < n; i++) {
                char *uid = cJSON_GetStringValue(cJSON_GetArrayItem(ids, i));
                if (uid) dcast_dave_track_user(uid);
            }
            printf("[media]: clients connect (%d)\n", n);
            break;
        }

        case 13: {
            char *uid = cJSON_GetStringValue(cJSON_GetObjectItem(m_data, "user_id"));
            if (uid) dcast_dave_untrack_user(uid);
            printf("[media]: client disconnect %s\n", uid ? uid : "?");
            break;
        }

        case 15: {
            char *dump = cJSON_PrintUnformatted(m_data);
            printf("[media]: quality feedback: %s\n", dump);
            free(dump);
            break;
        }

        case 6: break; // heartboeat
        case 21: case 22: case 24: dcast_dave_on_json(session, op->valueint, m_data); break;
        default: printf("[media]: unknown opcode:  %d\n", op->valueint);
    }
}