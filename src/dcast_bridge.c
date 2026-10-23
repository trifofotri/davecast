#define _GNU_SOURCE
#include <davecast.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

int dcast_inject(dcast_session *s, const char *json);

static int g_lfd = -1;

int dcast_bridge_listen(int port) {
    g_lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_lfd < 0) return -1;
    int one = 1;
    setsockopt(g_lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(g_lfd, (struct sockaddr *)&a, sizeof a) < 0 || listen(g_lfd, 4) < 0) {
        close(g_lfd); g_lfd = -1; return -1;
    }
    fcntl(g_lfd, F_SETFL, fcntl(g_lfd, F_GETFL, 0) | O_NONBLOCK);
    return 0;
}

static void reply(int fd, int code) {
    char r[96];
    int n = snprintf(r, sizeof r, "HTTP/1.1 %d X\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", code);
    (void)!write(fd, r, n);
}

int dcast_bridge_poll(dcast_session *s) {
    if (g_lfd < 0) return 0;
    int fd = accept(g_lfd, NULL, NULL);
    if (fd < 0) return 0;

    struct timeval tv = {0, 200000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    char buf[8192];
    size_t len = 0;
    char *body = NULL;
    while (len < sizeof buf - 1) {
        ssize_t n = recv(fd, buf + len, sizeof buf - 1 - len, 0);
        if (n <= 0) break;
        len += n;
        buf[len] = 0;
        body = strstr(buf, "\r\n\r\n");
        if (body) {
            body += 4;
            const char *cl = strcasestr(buf, "content-length:");
            size_t want = cl ? strtoul(cl + 15, NULL, 10) : 0;
            if ((size_t)(buf + len - body) >= want) break;
        }
    }

    int ok = 0;
    if (body && strncmp(buf, "POST /inject", 12) == 0 && strcasestr(buf, "x-davecast:")) {
        if (s->live) {
            reply(fd, 409);
            close(fd);
            return 0;
        }
        ok = dcast_inject(s, body) == 0;
    }
    reply(fd, ok ? 200 : 400);
    close(fd);
    return ok;
}