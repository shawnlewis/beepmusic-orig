#include <assert.h>
#include <unistd.h>

#include "beep/debug.h"
#include "beep/beeplib.h"

#include "beepnl.h"


static int beepnl_get_family_cb(struct nl_msg *msg, void *arg) {
    // Note: Not using NL80211_ATTR_MAX as this is the generic netlink.
    struct nlattr *tb[CTRL_ATTR_MAX + 1];
    struct nlattr *mcast_grp;
    struct nlattr *tb_mcast_grp[CTRL_ATTR_MCAST_GRP_MAX + 1];
    struct genlmsghdr *gnl_hdr = nlmsg_data(nlmsg_hdr(msg));
    struct beepnl_cb_args *cb_args = (struct beepnl_cb_args *)arg;
    struct beepnl_state *state = (struct beepnl_state *)cb_args->priv;
    char *mcast_str;
    int mcast_len;
    int rem;  // nla_for_each_nested remaining.
    uint32_t mcast_id;

    LOG_DEBUG(log_beep_main, "msg: %p arg: %p", msg, arg);

    nla_parse(tb, CTRL_ATTR_MAX, genlmsg_attrdata(gnl_hdr, 0),
          genlmsg_attrlen(gnl_hdr, 0), NULL);
    if (!tb[CTRL_ATTR_MCAST_GROUPS]) {
        LOG_ERROR(log_beep_main, "missing attr_mcast_groups");
        return NL_SKIP;
    }

    nla_for_each_nested(mcast_grp, tb[CTRL_ATTR_MCAST_GROUPS], rem) {
        nla_parse(tb_mcast_grp, CTRL_ATTR_MCAST_GRP_MAX,
                nla_data(mcast_grp), nla_len(mcast_grp), NULL);
        if (tb_mcast_grp[CTRL_ATTR_MCAST_GRP_NAME] &&
                tb_mcast_grp[CTRL_ATTR_MCAST_GRP_ID]) {
            mcast_id = nla_get_u32(tb_mcast_grp[CTRL_ATTR_MCAST_GRP_ID]);
            mcast_len = nla_len(tb_mcast_grp[CTRL_ATTR_MCAST_GRP_NAME]);
            mcast_str = (char *)nla_data(
                    tb_mcast_grp[CTRL_ATTR_MCAST_GRP_NAME]);

            LOG_DEBUG(log_beep_main, "name: %.*s id: %d", mcast_len,
                    mcast_str, mcast_id);

            if (!strncmp("config", mcast_str, mcast_len)) {
                state->mcast_config_id = (int)mcast_id;
            } else if (!strncmp("scan", mcast_str, mcast_len)) {
                state->mcast_scan_id = (int)mcast_id;
            } else if (!strncmp("regulatory", mcast_str, mcast_len)) {
                state->mcast_reg_id = (int)mcast_id;
            } else if (!strncmp("mlme", mcast_str, mcast_len)) {
                state->mcast_mlme_id = (int)mcast_id;
            }
        }
    }

    return NL_SKIP;
}

// Get family information which will contain the multicast ids for events.
// This is part of Linux generic netlink.
static int beepnl_get_mcast_ids(struct beepnl_state *state) {
    struct nl_msg *msg = NULL;
    struct beepnl_sendmsg_cb sendmsg_cb = {};
    int ret = 1;

    state->mcast_config_id = -1;
    state->mcast_scan_id = -1;
    state->mcast_reg_id = -1;
    state->mcast_mlme_id = -1;

    msg = nlmsg_alloc();

    // Get family cmd for nl80211.
    genlmsg_put(msg, 0, 0, state->ctrl_id, 0, 0, CTRL_CMD_GETFAMILY, 0);
    NLA_PUT_STRING(msg, CTRL_ATTR_FAMILY_NAME, "nl80211");

    sendmsg_cb.valid_cb = beepnl_get_family_cb;

    // beepnl_get_family_cb uses state for it's data.
    ret = beepnl_sendmsg(state, msg, &sendmsg_cb, state);

    if (ret) {
        LOG_ERROR(log_beep_main, "error sending get_family");
    } else if (state->mcast_config_id < 0
            || state->mcast_scan_id < 0
            || state->mcast_reg_id < 0
            || state->mcast_mlme_id < 0) {
        LOG_ERROR(log_beep_main, "did not get all mcast ids");
        ret = 1;
    }

nla_put_failure:  // Used in NLA_PUT
    if (msg)
        nlmsg_free(msg);

    return ret;
}

int beepnl_refresh(struct beepnl_state *state) {
    state->if_index = if_nametoindex(state->device);
    if (!state->if_index) {
        LOG_ERROR(log_beep_main, "invalid device %s", state->device);
        return 1;
    }
    return 0;
}

void beepnl_free(struct beepnl_state *state) {
    if (state) {
        if (state->device) {
            free(state->device);
        }
        if (state->nl_sock) {
            nl_socket_free(state->nl_sock);
        }
        if (state->nl_sock_nonblock) {
            nl_socket_free(state->nl_sock_nonblock);
        }
        free(state);
    }
}

struct beepnl_state *beepnl_init(const char *device) {
    struct beepnl_state *state;

    if (!device)
        return NULL;

    state = (struct beepnl_state *)malloc(sizeof(struct beepnl_state));
    if (!state) {
        return NULL;
    }
    memset(state, 0, sizeof(struct beepnl_state));

    state->device = strdup(device);
    if (!state->device) {
        goto error;
    }

    // Use two sockets and set one to nonblocking mode since libnl does not
    // have a function to set blocking mode.  Alternatively we could get the
    // fd (with nl_socket_get_fd) used for the socket and set it ourselves.
    state->nl_sock = nl_socket_alloc();
    state->nl_sock_nonblock = nl_socket_alloc();
    if (!state->nl_sock || !state->nl_sock_nonblock) {
        LOG_ERROR(log_beep_main, "could not alloc nl_sock");
        goto error;
    }

    if (genl_connect(state->nl_sock)
        || genl_connect(state->nl_sock_nonblock)) {
        LOG_ERROR(log_beep_main, "could not connect to nl");
        goto error;
    }

    if (nl_socket_set_nonblocking(state->nl_sock_nonblock)) {
        LOG_ERROR(log_beep_main, "could not set nonblock");
        goto error;
    }

    state->nl80211_id = genl_ctrl_resolve(state->nl_sock, "nl80211");
    if (state->nl80211_id < 0) {
        LOG_ERROR(log_beep_main, "invalid nl id");
        goto error;
    }

    state->ctrl_id = genl_ctrl_resolve(state->nl_sock, "nlctrl");
    if (state->ctrl_id < 0) {
        LOG_ERROR(log_beep_main, "invalid ctrl id");
        goto error;
    }

    state->if_index = if_nametoindex(state->device);
    if (!state->if_index) {
        LOG_ERROR(log_beep_main, "invalid device %s", state->device);
        goto error;
    }

    if (beepnl_get_mcast_ids(state)) {
        goto error;
    }

    return state;

error:
    beepnl_free(state);

    return NULL;
}

static int beepnl_err_cb(struct sockaddr_nl *nla, struct nlmsgerr *nlerr, void *arg) {
    LOG_ERROR(log_beep_main, "err: %d", nlerr->error);
    ((struct beepnl_cb_args *)(arg))->status = nlerr->error;
    return NL_STOP;
}

static int beepnl_fin_cb(struct nl_msg *msg, void *arg) {
    LOG_TRACE(log_beep_main, "called");
    ((struct beepnl_cb_args *)(arg))->status = 0;
    return NL_SKIP;
}

static int beepnl_ack_cb(struct nl_msg *msg, void *arg) {
    LOG_TRACE(log_beep_main, "called");
    ((struct beepnl_cb_args *)(arg))->status = 0;
    return NL_STOP;
}

// Note priv is not directly passed to the callbacks.  It will be the priv
// member of struct beepnl_cb_args.
int beepnl_sendmsg(struct beepnl_state *state, struct nl_msg *msg,
        struct beepnl_sendmsg_cb *sendmsg_cb, void *priv) {
    struct nl_cb *cb = nl_cb_alloc(NL_CB_DEFAULT);
    struct beepnl_cb_args cb_args = {1, priv};
    struct beepnl_sendmsg_cb sendmsg_cb_copy;

    if (!cb)
        return 1;

    if (sendmsg_cb) {
        // Don't overwrite values since the caller may be reusing this struct.
        memcpy(&sendmsg_cb_copy, sendmsg_cb, sizeof(struct beepnl_sendmsg_cb));
    } else {
        memset(&sendmsg_cb_copy, 0, sizeof(struct beepnl_sendmsg_cb));
    }
    // Set defaults for unset callbacks.
    sendmsg_cb_copy.fin_cb = sendmsg_cb_copy.fin_cb
            ? sendmsg_cb_copy.fin_cb : beepnl_fin_cb;
    sendmsg_cb_copy.ack_cb = sendmsg_cb_copy.ack_cb
            ? sendmsg_cb_copy.ack_cb : beepnl_ack_cb;
    sendmsg_cb_copy.err_cb = sendmsg_cb_copy.err_cb
            ? sendmsg_cb_copy.err_cb : beepnl_err_cb;

    if (sendmsg_cb_copy.err_cb != BEEPNL_DISABLE_CB) {
        nl_cb_err(cb, NL_CB_CUSTOM, sendmsg_cb_copy.err_cb, &cb_args);
    }

    if (sendmsg_cb_copy.fin_cb != BEEPNL_DISABLE_CB) {
        nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, sendmsg_cb_copy.fin_cb,
                &cb_args);
    }

    if (sendmsg_cb_copy.ack_cb != BEEPNL_DISABLE_CB) {
        nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, sendmsg_cb_copy.ack_cb,
                &cb_args);
    }

    if (sendmsg_cb_copy.valid_cb) {
        nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, sendmsg_cb_copy.valid_cb,
                &cb_args);
    }

    if (sendmsg_cb_copy.seq_cb) {
        nl_cb_set(cb, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, sendmsg_cb_copy.seq_cb,
                &cb_args);
    }

    // When listening to events we don't send a message.
    if (msg)
        nl_send_auto_complete(state->nl_sock, msg);

    if (sendmsg_cb_copy.timeout) {
        uint64_t start = beep_millis();
        do {
            nl_recvmsgs(state->nl_sock_nonblock, cb);
            if (cb_args.status <= 0)
                break;
            usleep(100 * 1000);  // 100ms delay.
        } while ((start + sendmsg_cb_copy.timeout) > beep_millis());
    } else {
        while (cb_args.status > 0) {
            nl_recvmsgs(state->nl_sock, cb);
        }
    }

    nl_cb_put(cb);

    // If this timed out cb_args.status will be 1, 0 for success, negative
    // for an error from libnl.
    return cb_args.status;
}

int beepnl_seq_check_ignore_cb(struct nl_msg *msg, void *arg) {
    return NL_OK;
}

int beepnl_add_membership(struct beepnl_state *state, bool nonblock) {
    int ret;
    struct nl_sock *mcast_sock = nonblock ? state->nl_sock_nonblock :
            state->nl_sock;

    ret = nl_socket_add_membership(mcast_sock,
            state->mcast_config_id);
    ret = ret ? ret : nl_socket_add_membership(mcast_sock,
            state->mcast_scan_id);
    ret = ret ? ret : nl_socket_add_membership(mcast_sock,
            state->mcast_reg_id);
    ret = ret ? ret : nl_socket_add_membership(mcast_sock,
            state->mcast_mlme_id);

    if (ret) {
        LOG_ERROR(log_beep_main, "failed to subscribe to events");
    }

    return ret;
}

void beepnl_drop_membership(struct beepnl_state *state, bool nonblock) {
    struct nl_sock *mcast_sock = nonblock ? state->nl_sock_nonblock :
            state->nl_sock;

    nl_socket_drop_membership(mcast_sock, state->mcast_config_id);
    nl_socket_drop_membership(mcast_sock, state->mcast_scan_id);
    nl_socket_drop_membership(mcast_sock, state->mcast_reg_id);
    nl_socket_drop_membership(mcast_sock, state->mcast_mlme_id);
}

int beepnl_event_subscribe(struct beepnl_state *state,
        nl_recvmsg_msg_cb_t event_cb, int timeout, void *priv) {
    struct beepnl_sendmsg_cb sendmsg_cb = {};
    int ret;

    if (!event_cb) {
        return 1;
    }

    sendmsg_cb.timeout = timeout;

    // Don't use the default callbacks, it's up to event_cb to clear
    // cb_args->status.
    sendmsg_cb.fin_cb = BEEPNL_DISABLE_CB;
    sendmsg_cb.ack_cb = BEEPNL_DISABLE_CB;
    sendmsg_cb.err_cb = BEEPNL_DISABLE_CB;
    // Need to disable the internal sequence checks from libnl.
    sendmsg_cb.seq_cb = beepnl_seq_check_ignore_cb;
    sendmsg_cb.valid_cb = event_cb;

    // Subscribe to all events.
    ret = beepnl_add_membership(state, timeout);

    if (!ret) {
        // There is no message to send since we're just listening.
        ret = beepnl_sendmsg(state, NULL, &sendmsg_cb, priv);
    }

    beepnl_drop_membership(state, timeout);

    return ret;
}
