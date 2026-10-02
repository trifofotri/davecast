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

void dcast_start_media(struct dcast_session* session) {
    char url[512];
    snprintf(url, sizeof(url), "wss://%s/?v=9", session->supdate_endpoint);

    session->media_wsocket = dcast_ws_connect(url, "media");

    cJSON *stream = cJSON_CreateObject();

    cJSON_AddStringToObject(stream, "type", "screen");
    cJSON_AddStringToObject(stream, "rid", "100");
    cJSON_AddNumberToObject(stream, "quality", 100);

    // streams array
    cJSON *streams = cJSON_CreateArray();
    cJSON_AddItemToArray(streams, stream);

    /* OP 0 = media identify */
    cJSON *d = cJSON_CreateObject();

    cJSON_AddStringToObject(d, "server_id", session->rtc_server);
    cJSON_AddStringToObject(d, "channel_id", session->rtc_channel);
    cJSON_AddStringToObject(d, "user_id", session->user_id);
    cJSON_AddStringToObject(d, "session_id", session->session_id);
    cJSON_AddStringToObject(d, "token", session->supdate_token);
    cJSON_AddNumberToObject(d, "max_dave_protocol_version", 1);
    cJSON_AddBoolToObject(d, "video", 1);
    cJSON_AddItemToObject(d, "streams", streams);

    cJSON *o = cJSON_CreateObject();

    cJSON_AddNumberToObject(o, "op", 0);
    cJSON_AddItemToObject(o, "d", d);

    dcast_ws_send(session->media_wsocket, o);
}