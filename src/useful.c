#include <dcast_media.h>
#include <davecast.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <useful.h>

unsigned long long dch_now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000ull + t.tv_nsec / 1000000ull;
}

void dch_udp_discover(struct dcast_session* session) {
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(session->media_port);
    if (inet_pton(AF_INET, session->media_ip, &sa.sin_addr) != 1) {
        puts("!! bad media ip from op2");
        return;
    }

    unsigned char req[74] = {0};
    req[1] = 1; req[3] = 70;
    req[4] = session->a_ssrc >> 24; req[5] = session->a_ssrc >> 16;
    req[6] = session->a_ssrc >> 8;  req[7] = session->a_ssrc;

    media_addr = sa;
    
    session->udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct timeval tv = {5, 0};
    setsockopt(session->udp_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    
    if (sendto(session->udp_fd, req, 74, 0, (struct sockaddr *)&sa, sizeof sa) != 74) return;

    unsigned char res[80] = {0};
    if (recv(session->udp_fd, res, sizeof res, 0) < 74 || res[1] != 2) {
        puts("udp discovery: no reply");
        return;
    }

    char ip[65];
    memcpy(ip, res + 8, 64);
    ip[64] = 0;
    
    snprintf(session->pub_ip, sizeof session->pub_ip, "%s", ip);
    session->pub_port = ( res[72] << 8 ) | res[73];
    fcntl(session->udp_fd, F_SETFL, fcntl(session->udp_fd, F_GETFL, 0) | O_NONBLOCK);
    printf("[udp] public endpoint: %s:%d\n", session->pub_ip, session->pub_port);
}