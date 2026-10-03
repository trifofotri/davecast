#ifndef DCAST_RTP_H
#define DCAST_RTP_H

#include <davecast.h>

int dcast_rtp_send_video(dcast_session *s, const void *annexb, size_t len, uint64_t pts_us, int keyframe);

#endif