#ifndef BEEP_NL_H
#define BEEP_NL_H

#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <net/if.h>

#include "nl80211.h"
#include "ie.h"


struct beepnl_state {
    char *device;
    struct nl_sock *nl_sock;
    struct nl_sock *nl_sock_nonblock;
    int nl80211_id;
    int ctrl_id;
    int if_index;
    int mcast_config_id;
    int mcast_scan_id;
    int mcast_reg_id;
    int mcast_mlme_id;
};

struct beepnl_cb_args {
    int status;
    void *priv;
};

#define BEEPNL_DEFAULT_CB                           (NULL)
#define BEEPNL_DISABLE_CB                           ((void *)(-1))
#define BEEPNL_WFE_ERROR                            (-1)

struct beepnl_sendmsg_cb {
    nl_recvmsg_msg_cb_t fin_cb;  // Will be set to beepnl defaults if NULL.
    nl_recvmsg_msg_cb_t ack_cb;  // Will be set to beepnl defaults if NULL.
    nl_recvmsg_msg_cb_t valid_cb;
    nl_recvmsg_msg_cb_t seq_cb;
    nl_recvmsg_err_cb_t err_cb;  // Will be set to beepnl defaults if NULL.
    int timeout;  // In millis, set to 0 for unlimited.
};


// Call this after restarting network to get any values that might
// have changed.
int beepnl_refresh(struct beepnl_state *state);
void beepnl_free(struct beepnl_state *state);
struct beepnl_state *beepnl_init(const char *device);

int beepnl_sendmsg(struct beepnl_state *state, struct nl_msg *msg,
        struct beepnl_sendmsg_cb *sendmsg_cb, void *priv);
int beepnl_seq_check_ignore_cb(struct nl_msg *msg, void *arg);
int beepnl_add_membership(struct beepnl_state *state, bool nonblock);
void beepnl_drop_membership(struct beepnl_state *state, bool nonblock);
int beepnl_event_subscribe(struct beepnl_state *state,
        nl_recvmsg_msg_cb_t event_cb, int timeout, void *priv);

const char *beepnl_reasonstr(uint16_t reason);
const char *beepnl_statusstr(uint16_t status);


#endif  // BEEP_NL_H
