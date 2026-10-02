#include <cjson/cJSON.h>

#include <identify.h>

void dcast_identify(struct dcast_socket* wsock, const struct dcast_config* cfg) {
    cJSON* props_json = cJSON_CreateObject();

    cJSON_AddStringToObject(props_json, "os", "Linux");
    cJSON_AddStringToObject(props_json, "browser", "Discord Client");
    cJSON_AddStringToObject(props_json, "release_channel", "stable");
    cJSON_AddStringToObject(props_json, "client_version", cfg->client_version);
    cJSON_AddStringToObject(props_json, "os_arch", "x64");
    cJSON_AddStringToObject(props_json, "app_arch", "x64");
    cJSON_AddStringToObject(props_json, "system_locale", "en-US");
    cJSON_AddStringToObject(props_json, "browser_user_agent", cfg->user_agent);
    cJSON_AddNumberToObject(props_json, "client_build_number", cfg->client_build);
    cJSON_AddNullToObject(props_json, "client_event_source");

    cJSON *client_state = cJSON_CreateObject();
    cJSON_AddItemToObject(client_state, "guild_versions", cJSON_CreateObject());

    cJSON *d = cJSON_CreateObject();

    cJSON_AddStringToObject(d, "token", cfg->token);
    cJSON_AddNumberToObject(d, "capabilities", cfg->capabilities);
    cJSON_AddItemToObject(d, "properties", props_json);
    cJSON_AddItemToObject(d, "client_state", client_state);

    cJSON *payload = cJSON_CreateObject();

    cJSON_AddNumberToObject(payload, "op", 2);
    cJSON_AddItemToObject(payload, "d", d);

    dcast_ws_send(wsock, payload);
}