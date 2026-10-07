#include <davecast.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <ws.h>

struct dcast_socket* dcast_ws_connect(const char *url, const char *name) {
    dcast_socket *w = calloc(1, sizeof *w);
    w->name = name;
    w->buf = malloc(BUFCAP);
    w->c = curl_easy_init();
    curl_easy_setopt(w->c, CURLOPT_URL, url);
    curl_easy_setopt(w->c, CURLOPT_CONNECT_ONLY, 2L);
    
    CURLcode r = curl_easy_perform(w->c);
    if (r != CURLE_OK) {
        fprintf(stderr, "%s: %s\n", name, curl_easy_strerror(r));
        return NULL;
    }

    curl_easy_getinfo(w->c, CURLINFO_ACTIVESOCKET, &w->fd);
    fcntl(w->fd, F_SETFL, fcntl(w->fd, F_GETFL, 0) | O_NONBLOCK);
    DCAST_DEBUG("[%s] connected: %s\n", name, url);
    return w;
}

int dcast_ws_pump(dcast_socket* wsock) {
    const struct curl_ws_frame *frame = NULL;
    static char tmp[65536];
    size_t n = 0;

    CURLcode r = curl_ws_recv(wsock->c, tmp, sizeof tmp, &n, &frame);
    if (r == CURLE_AGAIN) {
        return 0;
    }

    if (r != CURLE_OK) {
        fprintf(stderr, "[%s] recv error: %s\n", wsock->name, curl_easy_strerror(r));
        return -1;
    }

    if (frame && (frame->flags & CURLWS_CLOSE)) {
        int code = n >= 2 ? ((unsigned char)tmp[0] << 8) | (unsigned char)tmp[1] : 0;
        fprintf(stderr, "[%s] server closed the socket, close code %d%s\n", wsock->name, code, code == 4017 ? "  (DAVE/E2EE required -> rerun with the 'dave' arg)" : "");
        return -1;
    }

    if (frame && (frame->flags & (CURLWS_PING | CURLWS_PONG)))
        return 2;

    if (n) {
        if (wsock->len + n >= BUFCAP) { /* check if what we got is over the max */
            fprintf(stderr, "[%s] message too large (limit is %u)\n", wsock->name, BUFCAP);
            return -1;
        }

        memcpy(wsock->buf + wsock->len, tmp, n);
        wsock->len += n;
        wsock->buf[wsock->len] = 0;
    }

    if (frame && frame->bytesleft == 0 && !(frame->flags & CURLWS_CONT) && wsock->len) {
        wsock->mlen = wsock->len;
        wsock->len = 0;
        return 1;
    }

    return 2;
}

void dcast_ws_send(dcast_socket* wsock, cJSON* json_obj) {
    char *json_str = cJSON_Print(json_obj);
    size_t sent = 0;
    CURLcode r;
    int tries = 0;
    do {                                   /* socket is non-blocking: retry on AGAIN */
        r = curl_ws_send(wsock->c, json_str, strlen(json_str), &sent, 0, CURLWS_TEXT);
        if (r == CURLE_AGAIN) {
            usleep(10000);
        }
    } while (r == CURLE_AGAIN && ++tries < 500);

    free(json_str);
    cJSON_Delete(json_obj);
}

void dcast_ws_send_binary(dcast_socket* wsock, unsigned char op, const unsigned char *payload, size_t len) {
    unsigned char *buf = malloc(len + 1);
    buf[0] = op;
    if (len) {
        memcpy(buf + 1, payload, len);
    }
    size_t sent = 0;
    CURLcode r;
    int tries = 0;
    do {
        r = curl_ws_send(wsock->c, buf, len + 1, &sent, 0, CURLWS_BINARY);
        if (r == CURLE_AGAIN) {
            usleep(10000);
        }
    } while (r == CURLE_AGAIN && ++tries < 500);
    free(buf);
    if (r != CURLE_OK) fprintf(stderr, "[%s] binary send failed\n", wsock->name);
}