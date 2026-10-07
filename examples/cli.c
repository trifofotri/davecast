// examples/test_stream.c
//
// usage: test_stream <token> <guild_id> <voice_channel_id> <file.h264> [fps] [--attach]
//
// --attach: mid-call mode. davecast does NOT join the voice channel itself;
// it attaches to the voice state your REAL Discord client created. Join the
// channel in the real client (mic on) before or after starting this. The
// token is still required -- davecast still opens its own gateway for
// identify + op18/op22; only the voice join is skipped.
#define DCAST_DEBUG
#include <davecast.h>
#include <dcast_dave.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static volatile sig_atomic_t stop_flag = 0;
static void on_sigint(int sig) { (void)sig; stop_flag = 1; }

static unsigned long long now_us(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000000ull + t.tv_nsec / 1000ull;
}
static unsigned long long now_ms(void) { return now_us() / 1000ull; }

static size_t find_start_code(const unsigned char *b, size_t n, size_t from, int *cl) {
    for (size_t i = from; i + 3 <= n; i++) {
        if (b[i] == 0 && b[i+1] == 0 && b[i+2] == 1) { *cl = 3; return i; }
        if (i + 4 <= n && b[i] == 0 && b[i+1] == 0 && b[i+2] == 0 && b[i+3] == 1) { *cl = 4; return i; }
    }
    return n;
}

typedef struct {
    unsigned char *data; size_t size;
    size_t *au_start, *au_end;
    int *au_is_idr;
    int n_au;
} video_src;

static int load_video(const char *path, video_src *v) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); return 0; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return 0; }
    v->data = malloc((size_t)sz); v->size = (size_t)sz;
    if (fread(v->data, 1, v->size, f) != v->size) { fclose(f); return 0; }
    fclose(f);

    size_t cap = 4096, n = 0;
    size_t *off = malloc(cap * sizeof *off);
    int *type = malloc(cap * sizeof *type);
    int cl; size_t pos = find_start_code(v->data, v->size, 0, &cl);
    while (pos < v->size) {
        size_t nal_start = pos + cl;
        if (nal_start >= v->size) break;
        if (n == cap) { cap *= 2; off = realloc(off, cap*sizeof*off); type = realloc(type, cap*sizeof*type); }
        off[n] = pos; type[n] = v->data[nal_start] & 0x1F; n++;
        pos = find_start_code(v->data, v->size, nal_start, &cl);
    }
    if (n == 0) { fprintf(stderr, "no NALs found (not Annex-B h264?)\n"); return 0; }

    v->au_start = malloc(n * sizeof *v->au_start);
    v->au_end   = malloc(n * sizeof *v->au_end);
    v->au_is_idr = malloc(n * sizeof *v->au_is_idr);
    v->n_au = 0;
    size_t au_begin = off[0]; int has_vcl = 0, has_idr = 0;
    for (size_t i = 0; i < n; i++) {
        int vcl = (type[i] == 1 || type[i] == 5);
        if (vcl && has_vcl) {
            v->au_start[v->n_au] = au_begin;
            v->au_end[v->n_au] = off[i];
            v->au_is_idr[v->n_au] = has_idr;
            v->n_au++;
            au_begin = off[i]; has_vcl = 0; has_idr = 0;
        }
        if (vcl) { has_vcl = 1; if (type[i] == 5) has_idr = 1; }
    }
    v->au_start[v->n_au] = au_begin;
    v->au_end[v->n_au] = v->size;
    v->au_is_idr[v->n_au] = has_idr;
    v->n_au++;
    free(off); free(type);

    int n_idr = 0;
    for (int i = 0; i < v->n_au; i++) n_idr += v->au_is_idr[i];
    printf("[video] loaded %s: %d access units (%d are keyframes%s)\n",
           path, v->n_au, n_idr, n_idr ? "" : " -- REGENERATE THE FILE, PLI RECOVERY NEEDS THEM");
    return 1;
}

int main(int argc, char **argv) {
    /* flag parsing: strip --attach anywhere, keep positionals in order */
    const char *args[8]; int nargs = 0, attach = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--attach")) { attach = 1; continue; }
        if (nargs < 8) args[nargs++] = argv[i];
    }
    if (nargs < 4) {
        fprintf(stderr, "usage: %s <token> <guild_id> <voice_channel_id> <file.h264> [fps] [--attach]\n", argv[0]);
        return 1;
    }
    int fps = nargs > 4 ? atoi(args[4]) : 30;

    video_src v;
    if (!load_video(args[3], &v)) { fprintf(stderr, "could not load video\n"); return 1; }

    dcast_config cfg = {0};
    cfg.token = args[0]; cfg.guild_id = args[1]; cfg.channel_id = args[2];
    cfg.client_version = "1.0.154"; cfg.client_build = 626571;
    cfg.user_agent = "Mozilla/5.0 (X11; Linux x86_64) discord/1.0.154 Chrome/140.0.0.0";
    cfg.capabilities = 1767421;
    cfg.dave = DCAST_DAVE_REQUIRE;
    cfg.attach = attach;
    cfg.video.width = 1280; cfg.video.height = 720;
    cfg.video.framerate = fps; cfg.video.kbps = 2000;   /* matches the ffmpeg cmd */

    dcast_session *s = dcast_connect(&cfg);
    if (!s) { fprintf(stderr, "dcast_connect failed\n"); return 1; }
    signal(SIGINT, on_sigint);

    if (attach) {
        printf("*** attach mode: davecast will NOT join voice.\n"
               "*** 1. join the voice channel in your REAL client (mic on)\n"
               "*** 2. your mic must STAY connected through the whole test\n"
               "*** 3. watch the stream from a second account or the real client\n");
    }

    /* state-change tracker: prints each milestone exactly once */
    int seen_ready = 0, seen_requested = 0, seen_su = 0, seen_media = 0,
        seen_dave = 0, seen_live = 0, su_warned = 0;
    unsigned long long requested_at = 0, last_hint = 0;

    int cur = 0;
    unsigned long long next_frame_due = 0, pts = 0;
    unsigned long long frame_interval_us = 1000000ull / fps;
    int pli_recoveries = 0;

    while (!stop_flag) {
        int r = dcast_poll(s, 0);
        if (r != 0) { fprintf(stderr, "session dead (r=%d)\n", r); break; }

        if (!seen_ready && s->user_id[0]) {
            seen_ready = 1;
            printf("[state] READY  user=%s\n", s->user_id);
        }
        if (!seen_requested && s->stream_requested) {
            seen_requested = 1; requested_at = now_ms();
            printf("[state] op18+op22 sent -- stream requested%s\n",
                   attach ? " (attached to the REAL client's voice state)" : "");
        }
        if (!seen_su && s->havesupdate) {
            seen_su = 1;
            printf("[state] STREAM_SERVER_UPDATE  endpoint=%s\n", s->supdate_endpoint);
        }
        if (!seen_media && s->media_wsocket) {
            seen_media = 1;
            printf("[state] media socket connected\n");
        }
        if (!seen_dave && dcast_dave_state.established) {
            seen_dave = 1;
            printf("[state] DAVE E2EE established\n");
        }
        if (!seen_live && s->live) {
            seen_live = 1;
            printf("[state] *** LIVE -- sending frames ***\n");
        }

        /* attach mode: remind the human while we wait for their voice state */
        if (attach && !s->stream_requested && now_ms() - last_hint >= 5000) {
            last_hint = now_ms();
            printf("[attach] waiting for your voice state -- "
                   "join the channel in the real client\n");
        }

        /* THE experiment watchdog: op18 went out, did the server answer? */
        if (seen_requested && !seen_su && !su_warned &&
            now_ms() - requested_at > 10000) {
            su_warned = 1;
            if (attach) {
                fprintf(stderr,
                    "!! no STREAM_SERVER_UPDATE 10s after op18 in attach mode.\n"
                    "   The server likely refuses stream creation from a second\n"
                    "   gateway session -- the Vencord plugin must send op18\n"
                    "   through the client's own socket instead (plan B).\n");
            } else {
                fprintf(stderr,
                    "!! no STREAM_SERVER_UPDATE 10s after op18 -- check the\n"
                    "   guild/channel ids and the stream_key\n");
            }
        }

        if (s->pli_pending) {
            s->pli_pending = 0;
            int j = cur;
            do { j = (j + 1) % v.n_au; } while (j != cur && !v.au_is_idr[j]);
            if (v.au_is_idr[j]) {
                cur = j;
                pli_recoveries++;
                printf("[video] PLI recovery #%d -- jumping to keyframe at AU %d\n",
                       pli_recoveries, cur);
            } else {
                fprintf(stderr, "[video] PLI received but no keyframe in the file!\n");
            }
        }

        if (s->live && (!s->dave_active || dcast_dave_state.established)) {
            unsigned long long n = now_us();
            if (next_frame_due == 0) next_frame_due = n;
            if (n >= next_frame_due) {
                size_t off = v.au_start[cur], len = v.au_end[cur] - v.au_start[cur];
                dcast_send_video(s, v.data + off, len, pts, v.au_is_idr[cur]);
                if (cur % 90 == 0) printf("[video] frame: %d/%d\n", cur, v.n_au);
                cur = (cur + 1) % v.n_au;
                pts += frame_interval_us;
                next_frame_due += frame_interval_us;
                if (next_frame_due < n) next_frame_due = n + frame_interval_us;
            }
        }
        usleep(5000);
    }

    printf("\n*** %s\n", stop_flag ? "Ctrl-C -- stopping" : "session over");
    /* If you wire in a stop function, remember: in attach mode it must send
     * op19 ONLY. A voice-leave (op4 with null channel) would kick the real
     * client out of the call. */
    return 0;
}