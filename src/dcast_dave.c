#include <dcast_dave.h>
#include <davecast.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// global
dave_state dcast_dave_state;

void dcast_dave_fail_cb(const char *source, const char *reason, void *ud) {
    (void)ud;
    fprintf(stderr, "[dave] MLS FAILURE in %s: %s\n", source, reason);
}

void dcast_dave_track_user(const char *uid) {
    if (!uid || !*uid) return;
    for (int i = 0; i < dcast_dave_state.n_users; i++) { // check if already exists
        if (strcmp(dcast_dave_state.users[i], uid) == 0) return;
    }
    if (dcast_dave_state.n_users < DAVE_MAX_USERS) {
        snprintf(dcast_dave_state.users[dcast_dave_state.n_users++], 24, "%s", uid ? uid : "");
    }
}

void dcast_dave_untrack_user(const char *uid) {
    for (int i = 0; i < dcast_dave_state.n_users; i++) {
        if (strcmp(dcast_dave_state.users[i], uid) == 0) {
            memmove(&dcast_dave_state.users[i], &dcast_dave_state.users[i+1], (dcast_dave_state.n_users-i-1)*24);
            dcast_dave_state.n_users--;
            return;
        }
    }
}

int dcast_dave_recognized(struct dcast_session *session, const char *out[DAVE_MAX_USERS + 1]) {
    int n = 0;
    out[n++] = session->user_id;
    for (int i = 0; i < dcast_dave_state.n_users; i++) {
        out[n++] = dcast_dave_state.users[i];
    }
    return n;
}

void dcast_dave_ensure_session(struct dcast_session *session) {
    if (!session->rtc_channel[0] || !dcast_dave_state.have_ext_sender) return;

    if (!dcast_dave_state.session_inited) {
        dcast_dave_state.sess = daveSessionCreate(NULL, NULL, dcast_dave_fail_cb, NULL);
        if (!dcast_dave_state.sess) {
            fprintf(stderr, "[dave] daveSessionCreate failed\n");
            return;
        }
        dcast_dave_state.session_inited = 1;
    }

    uint64_t group_id = strtoull(session->rtc_channel, NULL, 10);
    daveSessionInit(dcast_dave_state.sess, dcast_dave_state.version, group_id, session->user_id);
    daveSessionSetExternalSender(dcast_dave_state.sess, dcast_dave_state.ext_sender, dcast_dave_state.ext_sender_len);

    uint8_t *kp = NULL; size_t kplen = 0;
    daveSessionGetMarshalledKeyPackage(dcast_dave_state.sess, &kp, &kplen);
    if (kp && kplen) {
        dcast_ws_send_binary(session->media_wsocket, 26, kp, kplen);
        daveFree(kp);
    } else {
        fprintf(stderr, "[dave] empty key package, group id or external sender is probably wrong\n");
    }
}

void dcast_dave_on_established(struct dcast_session *session) {
    dcast_dave_state.established = 1;
    dcast_dave_state.enc = daveEncryptorCreate();

    DAVEKeyRatchetHandle rk = daveSessionGetKeyRatchet(dcast_dave_state.sess, session->user_id);
    if (!rk) {
        fprintf(stderr, "[dave] GetKeyRatchet(self) failed\n");
        return;
    }
    daveEncryptorSetKeyRatchet(dcast_dave_state.enc, rk);
    daveKeyRatchetDestroy(rk);
    daveEncryptorAssignSsrcToCodec(dcast_dave_state.enc, session->v_ssrc, DAVE_CODEC_H264);
    daveEncryptorAssignSsrcToCodec(dcast_dave_state.enc, session->a_ssrc, DAVE_CODEC_OPUS);
    printf("[dave]: DAVE E2EE established!!!!!!\n");
}

/* op25 payload: ExternalSender bytes */
void dcast_dave_on_external_sender(struct dcast_session *session, const unsigned char *b, size_t n) {
    free(dcast_dave_state.ext_sender);
    dcast_dave_state.ext_sender = malloc(n);
    memcpy(dcast_dave_state.ext_sender, b, n);
    dcast_dave_state.ext_sender_len = n;
    dcast_dave_state.have_ext_sender = 1;
    printf("[dave]:  external sender package (%zu bytes)\n", n);
    dcast_dave_ensure_session(session);
}

/* op27 payload: proposals */
void dcast_dave_on_proposals(struct dcast_session *session, const unsigned char *b, size_t n) {
    if (!dcast_dave_state.sess) { fprintf(stderr, "[dave] op27 with no session yet\n"); return; }
    const char *rec[DAVE_MAX_USERS + 1];
    int nrec = dcast_dave_recognized(session, rec);
    uint8_t *out = NULL;
    size_t outlen = 0;
    daveSessionProcessProposals(dcast_dave_state.sess, b, n, rec, nrec, &out, &outlen);
    
    if (out && outlen) {
        dcast_ws_send_binary(session->media_wsocket, 28, out, outlen);
        daveFree(out);
    } else {
        fprintf(stderr, "[dave] ProcessProposals produced nothing\n");
    }
}

/* op29 payload: [u16 transition_id][MLS commit] */
void dcast_dave_on_commit(struct dcast_session *session, const unsigned char *b, size_t n) {
    if (n < 2 || !dcast_dave_state.sess) return;
    unsigned tid = ( b[0] << 8 ) | b[1];
    DAVECommitResultHandle r = daveSessionProcessCommit(dcast_dave_state.sess, b + 2, n - 2);
    int failed = !r || daveCommitResultIsFailed(r);
    int ignored = r && daveCommitResultIsIgnored(r);
    if (r) {
        daveCommitResultDestroy(r);
    }

    cJSON *d = cJSON_CreateObject();
    cJSON_AddNumberToObject(d, "transition_id", tid);

    if (failed) {
        fprintf(stderr, "[dave] commit FAILED (tid=%u), sending op31, resetting\n", tid);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "op", 31);
        cJSON_AddItemToObject(o, "d", d);
        dcast_ws_send(session->media_wsocket, o);   /* dcast_ws_send frees o+d */
        daveSessionReset(dcast_dave_state.sess);
        dcast_dave_ensure_session(session);
        return;
    }
    
    if (ignored) {
        cJSON_Delete(d);
        printf("[dave] commit ignored (tid: %u)\n", tid);
        return;
    }

    printf("[dave] commit OK, tid: %u\n", tid);
    if (!dcast_dave_state.established) dcast_dave_on_established(session);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "op", 23);
    cJSON_AddItemToObject(o, "d", d);
    dcast_ws_send(session->media_wsocket, o);
}

/* op30 payload: [u16 transition_id][Welcome] */
static void dcast_dave_on_welcome(struct dcast_session *session, const unsigned char *b, size_t n) {
    if (n < 2 || !dcast_dave_state.sess) return;
    unsigned tid = (b[0] << 8) | b[1];
    const char *rec[DAVE_MAX_USERS + 1];
    int nrec = dcast_dave_recognized(session, rec);
    DAVEWelcomeResultHandle r = daveSessionProcessWelcome(dcast_dave_state.sess, b + 2, n - 2, rec, nrec);

    cJSON *d = cJSON_CreateObject();
    cJSON_AddNumberToObject(d, "transition_id", tid);

    if (!r) {
        fprintf(stderr, "[dave] welcome failed for some reason (tid: %u), sending op31, resetting\n", tid);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "op", 31);
        cJSON_AddItemToObject(o, "d", d);
        dcast_ws_send(session->media_wsocket, o);
        daveSessionReset(dcast_dave_state.sess);
        dcast_dave_ensure_session(session);
        return;
    }
    daveWelcomeResultDestroy(r);
    printf("[dave] welcome OK, tid: %u\n", tid);
    if (!dcast_dave_state.established) dcast_dave_on_established(session);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "op", 23);
    cJSON_AddItemToObject(o, "d", d);
    dcast_ws_send(session->media_wsocket, o);
}

void dcast_dave_on_binary(struct dcast_session *session, const unsigned char *buf, size_t n) {
    if (n < 3) {
        fprintf(stderr, "[dave] binary frame too short (%zu)\n", n);
        return;
    }
    
    unsigned op = buf[2];
    const unsigned char *payload = buf + 3;
    size_t plen = n - 3;
    switch (op) {
        case 25: dcast_dave_on_external_sender(session, payload, plen); break;
        case 27: dcast_dave_on_proposals(session, payload, plen); break;
        case 29: dcast_dave_on_commit(session, payload, plen); break;
        case 30: dcast_dave_on_welcome(session, payload, plen); break;
        default: printf("[dave]: unhandled binary opcode: %u ( %zu bytes )\n", op, plen);
    }
}

// JSON ops 21 (prepare_transition), 22 (execute_transition), 24 (prepare_epoch)
void dcast_dave_on_json(struct dcast_session *session, int op, cJSON *d) {
    if (op == 24) {
        cJSON *ver = cJSON_GetObjectItem(d, "protocol_version");
        cJSON *epoch = cJSON_GetObjectItem(d, "epoch");
        if (cJSON_IsNumber(ver)) dcast_dave_state.version = (uint16_t)ver->valueint;
        if (cJSON_IsNumber(epoch) && epoch->valueint == 1) {
            if (dcast_dave_state.session_inited) {
                daveSessionReset(dcast_dave_state.sess);
            }
            dcast_dave_ensure_session(session);
        }
    }
}


