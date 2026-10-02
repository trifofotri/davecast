#include <streamandvoice.h>
#include <davecast.h>
#include <cjson/cJSON.h>
#include <dave/dave.h>
#include <dcast_dave.h>
#include <event.h>
#include <stdio.h>
#include <string.h>

void dcast_dispatch_event(struct dcast_session* session, cJSON* json_obj) {
    cJSON* event_data = cJSON_GetObjectItem(json_obj, "d");
    cJSON* event_type = cJSON_GetObjectItem(json_obj, "t");

    if (!cJSON_IsObject(event_data) || !cJSON_IsString(event_type)) return;

    const char *event_type_string = event_type->valuestring;

    if (strcmp(event_type_string, "READY") == 0) {
        cJSON* session_id = cJSON_GetObjectItem(event_data, "session_id");
        cJSON* user = cJSON_GetObjectItem(event_data, "user");
        cJSON* user_id = cJSON_GetObjectItem(user, "id");

        char* session_id_string = cJSON_GetStringValue(session_id);
        char* user_id_string = cJSON_GetStringValue(user_id);

        snprintf(session->user_id, sizeof(session->user_id), "%s", user_id_string);
        snprintf(session->session_id, sizeof(session->session_id), "%s", session_id_string);

        printf("[dispatcher] [READY]: session_id: %s, user_id: %s\n", session_id_string, user_id_string);
        
        join_voice(session); /* this will trigger the VOICE_STATE_UPDATE event which when detected will request a stream*/
        
    } else if (strcmp(event_type_string, "VOICE_STATE_UPDATE") == 0) {
        cJSON *user_id = cJSON_GetObjectItem(event_data, "user_id");
        cJSON *channel_id = cJSON_GetObjectItem(event_data, "channel_id");

        const char *user_id_string = cJSON_GetStringValue(user_id);
        const char *channel_id_string = cJSON_GetStringValue(channel_id);

        if (user_id_string && strcmp(user_id_string, session->user_id) == 0) {
            printf("[dispatcher] [VOICE_STATE_UPDATE]: channel: %s\n", channel_id_string ? channel_id_string : "none");

            if (channel_id_string && !session->stream_requested) {
                request_stream(session); /* request stream immediately */
            }
        }
    } else if (strcmp(event_type_string, "STREAM_CREATE") == 0) {
        cJSON* rtc_server = cJSON_GetObjectItem(event_data, "rtc_server_id");
        cJSON* rtc_channel = cJSON_GetObjectItem(event_data, "rtc_channel_id");
        cJSON* stream_key = cJSON_GetObjectItem(event_data, "stream_key");

        char* rtc_server_string  = cJSON_GetStringValue(rtc_server);
        char* rtc_channel_string = cJSON_GetStringValue(rtc_channel);
        char* stream_key_string  = cJSON_GetStringValue(stream_key);

        if (!rtc_server_string || !rtc_channel_string || !stream_key_string) return;

        snprintf(session->rtc_server,  sizeof(session->rtc_server),  "%s", rtc_server_string);
        snprintf(session->rtc_channel, sizeof(session->rtc_channel), "%s", rtc_channel_string);
        snprintf(session->stream_key,  sizeof(session->stream_key),  "%s", stream_key_string);

        printf("[dispatcher] [STREAM_CREATE]: rtc_server: %s rtc_channel: %s key: %s\n", session->rtc_server, session->rtc_channel, session->stream_key);
        
        dcast_dave_ensure_session(session);
    } else if (strcmp(event_type_string, "STREAM_SERVER_UPDATE") == 0) {
        cJSON* endpoint  = cJSON_GetObjectItem(event_data, "endpoint");
        char* endpoint_string  = cJSON_GetStringValue(endpoint);

        if (!endpoint || ! endpoint_string) {
            puts("[dispatcher] [STREAM_SERVER_UPDATE]: STREAM_SERVER_UPDATE with null endpoint!!!");
            return;
        }

        cJSON* token  = cJSON_GetObjectItem(event_data, "token");
        char* token_string  = cJSON_GetStringValue(token);

        snprintf(session->supdate_endpoint,  sizeof(session->supdate_endpoint),  "%s", endpoint_string);
        snprintf(session->supdate_token,  sizeof(session->supdate_token),  "%s", token_string);
        session->havesupdate = 1;

        printf("[dispatcher] [STREAM_SERVER_UPDATE]: endpoint: %s\n", endpoint_string);
        if (!session->media_wsocket) {
            dcast_start_media(session);
        }
    }
}