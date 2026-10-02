#include <streamandvoice.h>
#include <davecast.h>
#include <cjson/cJSON.h>
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
            printf("[dispatcher] [VOICE_STATE_UPDATE]: channel=%s\n", channel_id_string ? channel_id_string : "none");

            if (channel_id_string && !session->stream_requested) {
                request_stream(session); /* request stream immediately */
            }
        }
    }
}