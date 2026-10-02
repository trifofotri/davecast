#ifndef DCAST_DAVE_H
#define DCAST_DAVE_H

#include <dave/dave.h>
#include <davecast.h>

#define DAVE_MAX_USERS 64

typedef struct dave_state {
    DAVESessionHandle sess;
    DAVEEncryptorHandle enc;
    uint16_t version;
    unsigned char *ext_sender;    // cache raw op25
    size_t ext_sender_len;
    int have_ext_sender;
    int session_inited;           /* daveSessionInit called at least once */
    char users[DAVE_MAX_USERS][24];
    int n_users;
    int established;              /* currentState_ exists: safe to encrypt */
} dave_state;

extern dave_state dcast_dave_state;

void dcast_dave_track_user(const char *uid);
void dcast_dave_untrack_user(const char *uid);
void dcast_dave_fail_cb(const char *source, const char *reason, void *ud);
int dcast_dave_recognized(struct dcast_session *session, const char *out[DAVE_MAX_USERS + 1]);
void dcast_dave_ensure_session(struct dcast_session *session);
void dcast_dave_on_established(struct dcast_session *session);
void dcast_dave_on_external_sender(struct dcast_session *session, const unsigned char *b, size_t n);
void dcast_dave_on_proposals(struct dcast_session *session, const unsigned char *b, size_t n);
void dcast_dave_on_commit(struct dcast_session *session, const unsigned char *b, size_t n);
static void dcast_dave_on_welcome(struct dcast_session *session, const unsigned char *b, size_t n);
void dcast_dave_on_binary(struct dcast_session *session, const unsigned char *buf, size_t n);
void dcast_dave_on_json(struct dcast_session *session, int op, cJSON *d);

#endif