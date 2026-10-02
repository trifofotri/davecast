#include <davecast.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("%s <token>\n", argv[0]);
        return 1;
    }

    struct dcast_config* cfg = malloc(sizeof(dcast_config));
    cfg->guild_id = "1555335414544470126";
    cfg->channel_id = "1555335425919549581";
    cfg->dave = DCAST_DAVE_REQUIRE;
    cfg->client_version = "1.0.154";
    char browser_ua[256];
    sprintf(browser_ua, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) discord/ %s Chrome/140.0.0.0 Electron/38.0.0 Safari/537.36", cfg->client_version);
    cfg->capabilities = 1767421;
    cfg->token = argv[1];

    dcast_event_cb cb;
    dcast_connect(cfg, cb, NULL);
}