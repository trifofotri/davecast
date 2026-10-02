#include <streamandvoice.h>
#include <ws.h>
#include <cjson/cJSON.h>
#include <stdio.h>

void join_voice(struct dcast_session *session) {
    cJSON *d = cJSON_CreateObject();

    cJSON_AddStringToObject(d, "guild_id", session->cfg->guild_id);

    cJSON_AddStringToObject(d, "channel_id", session->cfg->channel_id);

    cJSON_AddBoolToObject(d, "self_mute", 0);
    cJSON_AddBoolToObject(d, "self_deaf", 0);
    cJSON_AddBoolToObject(d, "self_video", 0);
    cJSON_AddNumberToObject(d, "flags", 0);

    cJSON *o = cJSON_CreateObject();

    cJSON_AddNumberToObject(o, "op", 4);
    cJSON_AddItemToObject(o, "d", d);

    dcast_ws_send(session->gateway_wsocket, o);
}

void request_stream(struct dcast_session* session) {
    session->stream_requested = 1;

    /* build the stream key */
    snprintf(session->stream_key, sizeof(session->stream_key), "guild:%s:%s:%s", session->cfg->guild_id, session->cfg->channel_id, session->user_id);

    /* OP 18 = start stream */
    cJSON *c = cJSON_CreateObject();

    cJSON_AddStringToObject(c, "type", "guild");
    cJSON_AddStringToObject(c, "guild_id", session->cfg->guild_id);
    cJSON_AddStringToObject(c, "channel_id", session->cfg->channel_id);
    cJSON_AddNullToObject(c, "preferred_region");

    cJSON *o = cJSON_CreateObject();

    cJSON_AddNumberToObject(o, "op", 18);
    cJSON_AddItemToObject(o, "d", c);

    dcast_ws_send(session->gateway_wsocket, o);

    /* OP 22 */
    cJSON *p = cJSON_CreateObject();

    cJSON_AddStringToObject(p, "stream_key", session->stream_key);
    cJSON_AddBoolToObject(p, "paused", 0);

    o = cJSON_CreateObject();

    cJSON_AddNumberToObject(o, "op", 22);
    cJSON_AddItemToObject(o, "d", p);

    dcast_ws_send(session->gateway_wsocket, o);
}