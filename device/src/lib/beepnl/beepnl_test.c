#include <assert.h>

#include "beep/debug.h"
#include "beepnl/beepnl.h"

// nl80211.h is going to have most of the definitions for this structure.
// Some may not have the type information and you have to look at
// the kernel source to find them (linux/net/wireless/nl80211.c).
static struct nla_policy sta_info_policy[NL80211_STA_INFO_MAX + 1] = {
    [NL80211_STA_INFO_INACTIVE_TIME] = { .type = NLA_U32 },
    [NL80211_STA_INFO_RX_BYTES] = { .type = NLA_U32 },
    [NL80211_STA_INFO_TX_BYTES] = { .type = NLA_U32 },
    [NL80211_STA_INFO_LLID] = { .type = NLA_UNSPEC },
    [NL80211_STA_INFO_PLID] = { .type = NLA_UNSPEC },
    [NL80211_STA_INFO_PLINK_STATE] = { .type = NLA_U32 },
    [NL80211_STA_INFO_SIGNAL] = { .type = NLA_U8 },
    [NL80211_STA_INFO_TX_BITRATE] = { .type = NLA_NESTED },
    [NL80211_STA_INFO_RX_PACKETS] = { .type = NLA_U32 },
    [NL80211_STA_INFO_TX_PACKETS] = { .type = NLA_U32 },
    [NL80211_STA_INFO_TX_RETRIES] = { .type = NLA_U32 },
    [NL80211_STA_INFO_TX_FAILED] = { .type = NLA_U32 },
    [NL80211_STA_INFO_SIGNAL_AVG] = { .type = NLA_U8 },
    [NL80211_STA_INFO_RX_BITRATE] = { .type = NLA_NESTED },
    [NL80211_STA_INFO_BSS_PARAM] = { .type = NLA_NESTED },
    [NL80211_STA_INFO_CONNECTED_TIME] = { .type = NLA_UNSPEC },
    [NL80211_STA_INFO_STA_FLAGS] = {
        .type = NLA_UNSPEC,
        .minlen = sizeof(struct nl80211_sta_flag_update)
    },
    [NL80211_STA_INFO_BEACON_LOSS] = { .type = NLA_U32 }
};

static int nl_get_station_cb(struct nl_msg *msg, void *arg) {
    // nl80211.h is going to have the ATTR definitions.
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct nlattr *sta_info[NL80211_STA_INFO_MAX + 1];
    struct genlmsghdr *gnl_hdr = nlmsg_data(nlmsg_hdr(msg));
    // This cb_args is setup by beepnl_sendmsg.  It contains the status
    // and the priv passed into beepnl_sendmsg.  You only need to worry
    // about status if beepnl_sendmsg_cb.timeout is not 0.
    struct beepnl_cb_args *cb_args = (struct beepnl_cb_args *)arg;
    // This is the pointer passed in when you call beepnl_sendmsg.
    int8_t *sig_avg = (int8_t *)cb_args->priv;

    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnl_hdr, 0),
          genlmsg_attrlen(gnl_hdr, 0), NULL);

    if (!tb[NL80211_ATTR_STA_INFO]) {
        LOG_ERROR(log_beep_main, "sta info missing");
        // Return NL_SKIP to say you want to move on to the next message.
        return NL_SKIP;
    }

    if (nla_parse_nested(sta_info, NL80211_STA_INFO_MAX,
            tb[NL80211_ATTR_STA_INFO], sta_info_policy)) {
        LOG_ERROR(log_beep_main, "parsing sta info");
        return NL_SKIP;
    }

    if (sta_info[NL80211_STA_INFO_SIGNAL_AVG]) {
        int8_t val = nla_get_u8(sta_info[NL80211_STA_INFO_SIGNAL_AVG]);
        *sig_avg = val;
    } else {
        *sig_avg = 0;
    }

    return NL_SKIP;
}

int8_t get_sig_avg(struct beepnl_state *state) {
    struct nl_msg *msg = NULL;
    // Make sure this struct is zeroed out if you malloc it.
    struct beepnl_sendmsg_cb sendmsg_cb = {};
    int8_t sig_avg = 0;

    // Allocate a message for nl80211.
    msg = nlmsg_alloc();
    assert(msg);

    // Set the command and interface (other commands may need more
    // information).
    genlmsg_put(msg, 0, 0, state->nl80211_id, 0, NLM_F_DUMP,
            NL80211_CMD_GET_STATION, 0);
    NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, state->if_index);

    // Set the valid cb (nl80211's name not mine).
    sendmsg_cb.valid_cb = nl_get_station_cb;

    if (beepnl_sendmsg(state, msg, &sendmsg_cb, &sig_avg)) {
        LOG_ERROR(log_beep_main, "sendmsg failed");
        return 0;
    }

    return sig_avg;

nla_put_failure:
    LOG_ERROR(log_beep_main, "nla put error");

    if (msg)
        nlmsg_free(msg);

    return 0;
}

int main(int argc, char **argv) {
    struct beepnl_state *state;
    int8_t sig_avg;

    log_beep_main = LOG_CATEGORY_GET("beepnl-test");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    LOG_DEBUG(log_beep_main, "hello world");

    state = beepnl_init("wlan0");
    assert(state);

    sig_avg = get_sig_avg(state);

    LOG_INFO(log_beep_main, "signal avg: %d dBm", sig_avg);

    beepnl_free(state);

    return 0;
}
