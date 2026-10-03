# davecast

This is a library i made for messing around with discord screenshares.

You can use this to go live from plain C lol, it uses libcurl websockets and libdave.

You can feed this your own annexb H264 frames and stuff, it uses `andaead_aes256_gcm_rtpsize` for encryption.

<img width="880" height="653" alt="image" src="https://github.com/user-attachments/assets/7c8c272d-b79e-4e79-b537-f78bb0f151e9" />

## Status

Working E2E:
* gateway websocket: identification, voice join, stream creation/unpause
* media websocket: handshake, UDP discovery, session key acquisition
* DAVE: frame encryption via libdave, group stuff
* video: h.264 over rtp, 30–60 fps pacing

No audio or rtx retransmissions support yet! you can add that if you want!!


## Build

```sh
# 1) build libdave once (into third_party/libdave/cpp/build)
cd third_party/libdave/cpp && make

# 2) build davecast (this builds the example too)
cmake -B build && cmake --build build
```

you need libcurl with ws support!

## API

```c
#include <davecast.h>

dcast_config cfg = { 0 };
cfg.token = "token here";                /* ur user token */
cfg.guild_id = "guild id here";                /* guild + voice channel ids */
cfg.channel_id = "channel id here";
// width height framerate kbps
cfg.video = (dcast_video) { 1920, 1080, 60, 8000 };

dcast_session *s = dcast_connect( &cfg );

while (running && dcast_poll(s, 200) == 0) {   /* heartbeats + dispatch */
    if (s->live && frame_due()) {
        dcast_send_video(s, annexb, len, pts_us, is_idr);
    }
}
```

## Important

I'm not responsible for anything that happens to ur account when using this.
