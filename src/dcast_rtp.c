// dcast_rtp.c
#include <arpa/inet.h>
#include <davecast.h>
#include <dcast_dave.h>
#include <errno.h>
#include <openssl/evp.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>

#define VIDEO_PAYLOAD_TYPE 103
#define RTP_MAX_PAYLOAD    1100

static int gcm_encrypt(const unsigned char *key32, const unsigned char *nonce12, const unsigned char *aad, int aad_len, const unsigned char *pt, int pt_len, unsigned char *out, int *out_len) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int ok = ctx != NULL, len = 0, outl = 0, finl = 0;
    unsigned char tag[16];
    if (ok) ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1;
    if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) == 1;
    if (ok) ok = EVP_EncryptInit_ex(ctx, NULL, NULL, key32, nonce12) == 1;
    if (ok && aad_len) ok = EVP_EncryptUpdate(ctx, NULL, &len, aad, aad_len) == 1;
    if (ok) ok = EVP_EncryptUpdate(ctx, out, &outl, pt, pt_len) == 1;
    if (ok) ok = EVP_EncryptFinal_ex(ctx, out + outl, &finl) == 1;
    if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) == 1;
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    if (!ok) return 0;
    memcpy(out + outl + finl, tag, 16);
    *out_len = outl + finl + 16;
    return 1;
}

static void send_rtp_packet(struct dcast_session *s, int marker, const unsigned char *payload, int plen) {
    unsigned char hdr[12];
    hdr[0] = 0x80;
    hdr[1] = (unsigned char)((marker ? 0x80 : 0) | (VIDEO_PAYLOAD_TYPE & 0x7F));
    hdr[2] = s->rtp_seq >> 8;
    hdr[3] = s->rtp_seq & 0xFF;
    hdr[4] = s->rtp_ts >> 24; 
    hdr[5] = s->rtp_ts >> 16; 
    hdr[6] = s->rtp_ts >> 8; 
    hdr[7] = s->rtp_ts;
    hdr[8] = s->v_ssrc >> 24; 
    hdr[9] = s->v_ssrc >> 16;
    hdr[10] = s->v_ssrc >> 8;
    hdr[11] = s->v_ssrc;

    unsigned char nonce[12] = {0};
    nonce[0] = (unsigned char) (s->gcm_counter >> 24);
    nonce[1] = (unsigned char) (s->gcm_counter >> 16);
    nonce[2] = (unsigned char) (s->gcm_counter >> 8); 
    nonce[3] = (unsigned char) s->gcm_counter;

    unsigned char out[12 + RTP_MAX_PAYLOAD + 16 + 4];
    memcpy(out, hdr, 12);
    int clen = 0;
    if (!gcm_encrypt(s->key, nonce, hdr, 12, payload, plen, out + 12, &clen)) {
        fprintf(stderr, "[rtp] transport encrypt failed, dropping packet\n");
        return;
    }
    memcpy(out + 12 + clen, nonce, 4);
    
    size_t total = 12 + clen + 4;
    int tries = 0;
    ssize_t sent;
    for (;;) {
        sent = sendto(s->udp_fd, out, total, 0, (struct sockaddr *)&s->media_addr, sizeof s->media_addr);
        if (sent >= 0) break;
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            fprintf(stderr, "[rtp] sendto failed: %s\n", strerror(errno));
            break;
        }
        if (++tries > 50) {
            fprintf(stderr, "[rtp] sendto still EAGAIN after %d tries -- dropping (seq=%u)\n", tries, s->rtp_seq);
            break;
        }
        fd_set wf; FD_ZERO(&wf); FD_SET(s->udp_fd, &wf);
        struct timeval tv = {0, 2000};
        select(s->udp_fd + 1, NULL, &wf, NULL, &tv);
    }
    s->rtp_seq++; s->gcm_counter++;
}

static void send_nal(struct dcast_session *s, const unsigned char *nal, size_t nal_len, int marker) {
    if (nal_len == 0) return;
    if ((int)nal_len <= RTP_MAX_PAYLOAD) { send_rtp_packet(s, marker, nal, (int)nal_len); return; }

    unsigned char fu_indicator = (unsigned char)((nal[0] & 0xE0) | 28);
    unsigned char nal_type = nal[0] & 0x1F;
    size_t body_off = 1, remaining = nal_len - 1;
    int first = 1;
    unsigned char buf[2 + RTP_MAX_PAYLOAD];
    while (remaining > 0) {
        size_t chunk = remaining < (size_t)(RTP_MAX_PAYLOAD - 2) ? remaining : (size_t)(RTP_MAX_PAYLOAD - 2);
        int last = (chunk == remaining);
        buf[0] = fu_indicator;
        buf[1] = (unsigned char)((first ? 0x80 : 0) | (last ? 0x40 : 0) | nal_type);
        memcpy(buf + 2, nal + body_off, chunk);
        send_rtp_packet(s, marker && last, buf, (int)(chunk + 2));
        body_off += chunk; remaining -= chunk; first = 0;
    }
}

static size_t find_start_code(const unsigned char *b, size_t n, size_t from, int *code_len) {
    for (size_t i = from; i + 3 <= n; i++) {
        if (b[i] == 0 && b[i+1] == 0 && b[i+2] == 1) {
            *code_len = 3;
            return i;
        }

        if (i + 4 <= n && b[i] == 0 && b[i+1] == 0 && b[i+2] == 0 && b[i+3] == 1) {
            *code_len = 4;
            return i;
        }
    }
    return n;
}

static void packetize_frame(struct dcast_session *s, const unsigned char *buf, size_t len) {
    int cl;
    size_t pos = find_start_code(buf, len, 0, &cl);
    while (pos < len) {
        size_t nal_start = pos + cl;
        if (nal_start >= len) break;
        size_t next = find_start_code(buf, len, nal_start, &cl);
        send_nal(s, buf + nal_start, next - nal_start, next >= len);
        pos = next;
    }
}

int dcast_rtp_send_video(dcast_session *s, const void *annexb, size_t len, uint64_t pts_us, int keyframe) {
    if (!s->have_key) return -1;
    (void) keyframe;

    s->rtp_ts = (uint32_t) ( ( uint64_t) pts_us * 9 / 100 );

    if (s->dave_active && dcast_dave_state.established) {
        size_t cap = daveEncryptorGetMaxCiphertextByteSize(dcast_dave_state.enc, DAVE_MEDIA_TYPE_VIDEO, len);
        unsigned char *ct = malloc(cap);
        size_t written = 0;
        DAVEEncryptorResultCode rc = daveEncryptorEncrypt(dcast_dave_state.enc, DAVE_MEDIA_TYPE_VIDEO, s->v_ssrc, annexb, len, ct, cap, &written);
        if (rc != DAVE_ENCRYPTOR_RESULT_CODE_SUCCESS) {
            free(ct);
            return -2;
        }

        packetize_frame(s, ct, written);
        free(ct);
    } else if (s->dave_active) {
        return 0;
    } else {
        packetize_frame(s, annexb, len);
    }
    return 0;
}