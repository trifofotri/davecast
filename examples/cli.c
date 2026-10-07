// examples/test_stream.c
#include <davecast.h>
#include <dcast_dave.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static unsigned long long now_us(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000000ull + t.tv_nsec / 1000ull;
}

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
    int *au_is_idr;      /* 1 if this access unit contains a type-5 NAL */
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
    if (n == 0) { fprintf(stderr, "no NALs found\n"); return 0; }

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
    printf("[video] loaded %s: %d access units (%d are keyframes)\n", path, v->n_au,
           ({ int c = 0; for (int i = 0; i < v->n_au; i++) c += v->au_is_idr[i]; c; }));
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <token> <guild_id> <channel_id> <file.h264> [fps]\n", argv[0]);
        return 1;
    }
    int fps = argc > 5 ? atoi(argv[5]) : 30;

    video_src v;
    if (!load_video(argv[4], &v)) { fprintf(stderr, "could not load video\n"); return 1; }

    dcast_config cfg = {0};
    cfg.token = argv[1]; cfg.guild_id = argv[2]; cfg.channel_id = argv[3];
    cfg.client_version = "1.0.154"; cfg.client_build = 626571;
    cfg.user_agent = "Mozilla/5.0 (X11; Linux x86_64) discord/1.0.154 Chrome/140.0.0.0";
    cfg.capabilities = 1767421;
    cfg.dave = DCAST_DAVE_REQUIRE;
    cfg.video.width = 1280; cfg.video.height = 720; cfg.video.framerate = fps; cfg.video.kbps = 2000;

    dcast_session *s = dcast_connect(&cfg);
    if (!s) { fprintf(stderr, "dcast_connect failed\n"); return 1; }

    int cur = 0;
    unsigned long long next_frame_due = 0;
    unsigned long long pts = 0;
    unsigned long long frame_interval_us = 1000000ull / fps;
    int pli_recoveries = 0;

    for (;;) {
        int r = dcast_poll(s, 0);
        if (r != 0) { fprintf(stderr, "session dead (r=%d)\n", r); break; }

        if (s->pli_pending) {
            s->pli_pending = 0;
            int j = cur;
            do { j = (j + 1) % v.n_au; } while (j != cur && !v.au_is_idr[j]);
            if (v.au_is_idr[j]) {
                cur = j;
                pli_recoveries++;
                printf("[video] PLI recovery #%d -- jumping to keyframe at AU %d\n", pli_recoveries, cur);
            } else {
                fprintf(stderr, "[video] PLI received but no keyframe found anywhere in the file!\n");
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

    return 0;
}