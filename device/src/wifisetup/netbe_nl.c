#include <assert.h>
#include <unistd.h>

#include "beep/debug.h"
#include "beep/beeplib.h"

#include "wifisetup.h"
#include "netbe_ctrl.h"
#include "beepnl/beepnl.h"

static struct beepnl_state *state;


void ws_netbe_cleanup(void) {
    if (state) {
        beepnl_free(state);
        state = NULL;
    }
    ws_netbe_ctrl_cleanup();
}

int ws_netbe_init(void) {
    if (state) {
        LOG_WARN(log_beep_main, "called more than once");
        return 1;
    }

    if (ws_netbe_ctrl_init()) {
        return 1;
    }

    state = beepnl_init(ws_config->ifname);

    if (!state) {
        ws_netbe_cleanup();
    }

    return state ? 0 : 1;
}

struct wfe_cb_priv {
    int evt_count;
    const int *evts;
    int trigger_evt;
};

static int nl_wfe_cb(struct nl_msg *msg, void *arg) {
    struct beepnl_cb_args *cb_args = (struct beepnl_cb_args *)arg;
    struct wfe_cb_priv *priv = (struct wfe_cb_priv *)cb_args->priv;
    struct genlmsghdr *gnl_hdr = nlmsg_data(nlmsg_hdr(msg));
    int i;

    LOG_INFO(log_beep_main, "got nl80211 event: %d", gnl_hdr->cmd);

    if (priv->evt_count == 0) {
        priv->trigger_evt = BEEPNL_WFE_ERROR;
        cb_args->status = 0;
    }

    for (i = 0; i < priv->evt_count; i++) {
        if (gnl_hdr->cmd == priv->evts[i]) {
            priv->trigger_evt = gnl_hdr->cmd;
            cb_args->status = 0;
            break;
        }
    }

    return NL_SKIP;
}

struct if_info_cb_priv {
    OPMode op_mode;
    bool valid;
};

#define NL80211_GET_INTERFACE_MAX_RETRIES           (10)

static int nl_if_info_cb(struct nl_msg *msg, void *arg) {
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct beepnl_cb_args *cb_args = (struct beepnl_cb_args *)arg;
    struct if_info_cb_priv *priv = (struct if_info_cb_priv *)cb_args->priv;
    struct genlmsghdr *gnl_hdr = nlmsg_data(nlmsg_hdr(msg));

    //LOG_INFO(log_beep_main, "got if_info event");

    // Calling get_inteface can be ignored without error by nl80211 if the
    // interface is busy.  This lets the caller know if the callback
    // was actually called.
    priv->valid = true;

    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnl_hdr, 0),
          genlmsg_attrlen(gnl_hdr, 0), NULL);

    if (tb[NL80211_ATTR_IFTYPE]) {
        uint32_t iftype = nla_get_u32(tb[NL80211_ATTR_IFTYPE]);
        switch (iftype) {
        case NL80211_IFTYPE_STATION:
            priv->op_mode = OP_MODE_STA;
            break;

        case NL80211_IFTYPE_AP:
            priv->op_mode = OP_MODE_AP;
            break;

        case NL80211_IFTYPE_MONITOR:
            priv->op_mode = OP_MODE_MONITOR;
            break;

        default:
            LOG_WARN(log_beep_main, "unsupported iftype: %d", iftype);
            break;
        }
    }

    cb_args->status = 0;

    return NL_SKIP;
}

OPMode ws_netbe_op_mode(void) {
    struct nl_msg *msg = NULL;
    struct beepnl_sendmsg_cb sendmsg_cb = {};
    struct if_info_cb_priv if_info_priv = { OP_MODE_UNKNOWN, false };
    int ret = 0;
    int count = NL80211_GET_INTERFACE_MAX_RETRIES;

    sendmsg_cb.valid_cb = nl_if_info_cb;

    msg = nlmsg_alloc();

    // Query interface information.
    genlmsg_put(msg, 0, 0, state->nl80211_id, 0, 0,
            NL80211_CMD_GET_INTERFACE, 0);
    NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, state->if_index);

    while (if_info_priv.valid == false && !ret && count--) {
        ret = beepnl_sendmsg(state, msg, &sendmsg_cb, &if_info_priv);
    }

    if (ret) {
        LOG_ERROR(log_beep_main, "could not get interface information");
    }

nla_put_failure:  // Used in NLA_PUT
    if (msg)
        nlmsg_free(msg);

    return if_info_priv.op_mode;
}

// Not all of these are defined in nl80211.h (can be found in
// linux/net/wireless/nl80211.c).
static struct nla_policy bss_policy[NL80211_BSS_MAX + 1] = {
    [NL80211_BSS_BSSID] = { .type = NLA_UNSPEC },
    [NL80211_BSS_FREQUENCY] = { .type = NLA_U32 },
    [NL80211_BSS_TSF] = { .type = NLA_U64 },
    [NL80211_BSS_BEACON_INTERVAL] = { .type = NLA_U16 },
    [NL80211_BSS_CAPABILITY] = { .type = NLA_U16 },
    [NL80211_BSS_INFORMATION_ELEMENTS] = { .type = NLA_UNSPEC },
    [NL80211_BSS_SIGNAL_MBM] = { .type = NLA_U32 },
    [NL80211_BSS_SIGNAL_UNSPEC] = { .type = NLA_U8 },
    [NL80211_BSS_STATUS] = { .type = NLA_U32 },
    [NL80211_BSS_SEEN_MS_AGO] = { .type = NLA_U32 },
    [NL80211_BSS_BEACON_IES] = { .type = NLA_UNSPEC },
};

static int nl_get_scan_cb(struct nl_msg *msg, void *arg) {
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct nlattr *bss_tb[NL80211_BSS_MAX + 1];
    struct genlmsghdr *gnl_hdr = nlmsg_data(nlmsg_hdr(msg));
    struct beepnl_cb_args *cb_args = (struct beepnl_cb_args *)arg;
    uint8_t *info;
    AP *ap_head = *((AP **)(cb_args->priv));
    AP *new_ap;
    int info_len;
    int ret = 1;
    int sig_mbm;

    LOG_DEBUG(log_beep_main, "msg: %p arg: %p", msg, arg);

    new_ap = (AP *)malloc(sizeof(AP));
    if (!new_ap) {
        LOG_ERROR(log_beep_main, "could not alloc AP");
        return NL_STOP;
    }
    memset(new_ap, 0, sizeof(AP));

    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnl_hdr, 0),
          genlmsg_attrlen(gnl_hdr, 0), NULL);
    if (!tb[NL80211_ATTR_BSS]) {
        LOG_ERROR(log_beep_main, "missing attr_bss");
        goto done;
    }

    if (nla_parse_nested(bss_tb, NL80211_BSS_MAX, tb[NL80211_ATTR_BSS],
            bss_policy)) {
        LOG_ERROR(log_beep_main, "error parsing nested attr_bss");
        goto done;
    }

    if (!bss_tb[NL80211_BSS_BSSID]) {
        LOG_ERROR(log_beep_main, "missing bssid");
        goto done;
    }
    LOG_DEBUG(log_beep_main, "bssid: %s", bssidstr(
            (uint8_t *)nla_data(bss_tb[NL80211_BSS_BSSID])));
    memcpy(new_ap->bssid, nla_data(bss_tb[NL80211_BSS_BSSID]), 6);

// For some reason this is always empty, but NL80211_BSS_SIGNAL_MBM works.
//    if (!bss_tb[NL80211_BSS_SIGNAL_UNSPEC]) {
//        LOG_ERROR(log_beep_main, "missing signal unspec");
//        return NL_SKIP;
//    }
//    LOG_DEBUG(log_beep_main, "signal %d/100",
//            nla_get_u8(bss_tb[NL80211_BSS_SIGNAL_UNSPEC]));

    if (!bss_tb[NL80211_BSS_SIGNAL_MBM]) {
        LOG_ERROR(log_beep_main, "missing signal mbm");
        goto done;
    }
    sig_mbm = (int)nla_get_u32(bss_tb[NL80211_BSS_SIGNAL_MBM]);
    LOG_DEBUG(log_beep_main, "signal %d.%d", sig_mbm/100, sig_mbm%100);
    new_ap->signal = (int8_t)(sig_mbm / 100);

    if (!bss_tb[NL80211_BSS_INFORMATION_ELEMENTS]) {
        LOG_ERROR(log_beep_main, "missing info elements");
        goto done;
    }
    info = nla_data(bss_tb[NL80211_BSS_INFORMATION_ELEMENTS]);
    info_len = nla_len(bss_tb[NL80211_BSS_INFORMATION_ELEMENTS]);
    LOG_DEBUG(log_beep_main, "info: %p len: %d", info, info_len);

    // Run until info_len is less than min size (type-length) or less than
    // the length.
    while (info_len > 2 && info_len > info[1]) {
        //LOG_DEBUG(log_beep_main, "found: %d len %d", info[0], info[1]);
        switch (info[0]) {
        case BEEPNL_IE_ID_ESSID: {
            LOG_DEBUG(log_beep_main, "essid: %.*s", info[1], info + 2);
            new_ap->essid = (char *)malloc(info[1] + 1);
            if (new_ap->essid) {
                memcpy(new_ap->essid, info + 2, info[1]);
                new_ap->essid[info[1]] = '\0';
            }
            break;
        }

        case BEEPNL_IE_ID_DS: {
            LOG_DEBUG(log_beep_main, "channel: %d", info[2]);
            new_ap->channel = info[2];
            break;
        }

        case BEEPNL_IE_ID_RSN: {
            LOG_DEBUG(log_beep_main, "RSN IE found");
            new_ap->enc_type |= ENC_TYPE_WPA2;
            break;
        }

        case BEEPNL_IE_ID_VENDOR: {
            if (info[2 + 0] == BEEPNL_IE_OUI_MS_0
                && info[2 + 1] == BEEPNL_IE_OUI_MS_1
                && info[2 + 2] == BEEPNL_IE_OUI_MS_2
                && info[2 + 3] == BEEPNL_IE_VENDOR_TYPE_MS_WPA) {
                // TODO: Figure out if this is WPA or WEP.  Assume WPA for now.
                LOG_DEBUG(log_beep_main, "MS_OUI WPA IE found");
                new_ap->enc_type |= ENC_TYPE_WPA;
            }
        }

        default:
            break;
        }

        info_len -= info[1] + 2;
        info += info[1] + 2;
    }

    if (new_ap->enc_type == ENC_TYPE_UNKNOWN) {
        new_ap->enc_type = ENC_TYPE_NONE;
    }

    ret = 0;

done:
    if (ret) {
        free(new_ap);
    } else {
        if (!ap_head) {
            *((AP **)(cb_args->priv)) = new_ap;
        } else {
            // Place new_ap at the end of the ap list.
            while (ap_head->next)
                ap_head = ap_head->next;
            ap_head->next = new_ap;
        }
    }

    return NL_SKIP;
}

AP *ws_netbe_scan(void) {
    AP *ap_head = NULL;
    struct nl_msg *msg = NULL;
    struct nl_msg *ssid_msg = NULL;
    static const int scan_evts[] = {
        NL80211_CMD_SCAN_ABORTED,
        NL80211_CMD_NEW_SCAN_RESULTS
    };
    struct wfe_cb_priv wfe_priv = {
        sizeof(scan_evts)/sizeof(int),
        scan_evts,
        -1
    };
    struct beepnl_sendmsg_cb sendmsg_cb = {};
    int ret = 1;

    LOG_DEBUG(log_beep_main, "scan started on index %d", state->if_index);

    msg = nlmsg_alloc();
    ssid_msg = nlmsg_alloc();
    LOG_DEBUG(log_beep_main, "msg: %p ssid_msg: %p", msg, ssid_msg);

    // Trigger scan command.
    genlmsg_put(msg, 0, 0, state->nl80211_id, 0, 0,
            NL80211_CMD_TRIGGER_SCAN, 0);
    NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, state->if_index);

    // Scan for any SSIDs.
    NLA_PUT(ssid_msg, 1, 0, "");
    nla_put_nested(msg, NL80211_ATTR_SCAN_SSIDS, ssid_msg);

    ret = beepnl_sendmsg(state, msg, &sendmsg_cb, NULL);

    // Clear messages.
    nlmsg_free(msg);
    nlmsg_free(ssid_msg);
    ssid_msg = NULL;

    if (ret) {
        LOG_ERROR(log_beep_main, "could not trigger scan");
        return NULL;
    }

    ret = beepnl_event_subscribe(state, nl_wfe_cb, 0, &wfe_priv);
    LOG_INFO(log_beep_main, "return from events: %d with evt: %d", ret,
            wfe_priv.trigger_evt);

    if (wfe_priv.trigger_evt == NL80211_CMD_SCAN_ABORTED) {
        LOG_WARN(log_beep_main, "scan aborted");
        return NULL;
    }

    msg = nlmsg_alloc();
    LOG_DEBUG(log_beep_main, "msg: %p", msg);

    LOG_DEBUG(log_beep_main, "scan dump on index %d", state->if_index);

    // Get scan command.
    genlmsg_put(msg, 0, 0, state->nl80211_id, 0, NLM_F_DUMP,
            NL80211_CMD_GET_SCAN, 0);
    NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, state->if_index);

    // Setup nl_get_scan callback and send message.
    sendmsg_cb.valid_cb = nl_get_scan_cb;
    ret = beepnl_sendmsg(state, msg, &sendmsg_cb, &ap_head);
    if (ret) {
        LOG_ERROR(log_beep_main, "could not get scan information");
    }

nla_put_failure:  // Used in NLA_PUT
    if (msg)
        nlmsg_free(msg);
    if (ssid_msg)
        nlmsg_free(ssid_msg);

    return ret ? NULL : ap_head;
}

int ws_netbe_set_mode(BEMode be_mode) {
    OPMode exp_op_mode;
    OPMode cur_op_mode;
    BEMode cur_be_mode;
    int ret;

    switch (be_mode) {
    case BE_MODE_CLIENT:
        exp_op_mode = OP_MODE_STA;
        break;

    case BE_MODE_AP_SETUP:
        exp_op_mode = OP_MODE_AP;
        break;

    default:
        LOG_ERROR(log_beep_main, "invalid be_mode: %d", be_mode);
        return 1;
    }

    cur_op_mode = ws_netbe_op_mode();
    cur_be_mode = ws_netbe_ctrl_get_mode();

    // UCI settings and current network info are correct.
    if (cur_op_mode == exp_op_mode && cur_be_mode == be_mode)
        return 0;

    ret = ws_netbe_ctrl_set_mode(be_mode);
    if (ret)
        return ret;

    // This is a blind set.  Currently there is not a good way to get
    // if we are actually an AP from nl80211, we would need to communicate
    // with hostapd for that.  Changing to client mode may not need a return
    // status (i.e. shutting down wifisetup or forcing a connect).  It is
    // up to the caller to determine if the connect was successful.
    ret = ws_netbe_ctrl_net_restart();

    if (!ret)
        ret = beepnl_refresh(state);

    return ret;
}

#define WS_CONNECT_WAIT_NEW_STATION                 (0)
#define WS_CONNECT_WAIT_CONNECT                     (1)
#define WS_CONNECT_WAIT_DEL_STATION                 (2)

struct connect_wait_cb_priv {
    int wait;
    int last_event;
    int status;
    int reason;
    bool reason_by_ap;
};

// 802.11 reason and status codes are 0-65535.  Use negative for internal
// codes.  Avoid first -100 codes as reserved for wsd.
#define WS_CONNECT_STATUS_OK                        (-101)
#define WS_CONNECT_STATUS_NL_TIMEOUT                (-102)
#define WS_CONNECT_STATUS_UNKNOWN                   (-103)
#define WS_CONNECT_STATUS_DEL_STATION               (-104)
#define WS_CONNECT_REASON_OK                        (-101)
#define WS_CONNECT_REASON_NL_TIMEOUT                (-102)
#define WS_CONNECT_REASON_UNKNOWN                   (-103)

// Expected events:
// Note: we don't have to wait for each event, just enough to know
// the connection process is moving along (start connect, wait for connect
// wait for disconnect (error)).
//CMD   nl80211 CMD enum            Action (wifisetup timeout)
//
//19:   NL80211_CMD_NEW_STATION     New connect started, wait for
//                                  connect (t0).
//37:   NL80211_CMD_AUTHENTICATE    Check nl timeout attr and status (t1).
//38:   NL80211_CMD_ASSOCIATE       Check nl timeout attr and status (t1).
//46:   NL80211_CMD_CONNECT         Connection made, wait for del station
//                                  (t1).
//20:   NL80211_CMD_DEL_STATION     Connection failed.  If this times out we
//                                  can reasonably expect the connection
//                                  was successful.  If this does not time out
//                                  wait for deauth (t2).
//39:   NL80211_CMD_DEAUTHENTICATE  Wait for discon (t2).
//48:   NL80211_CMD_DISCONNECT      Check reason (t2).

// Description of timeouts (adjustable from command line).
//t0: first event timeout (evt 19).  Indicates connect was not properly
//      started.
//t1: t1 to connect timeout (evt 46).  Indicates we never were able to
//      find/associate/connect to the AP.
//t2: t2 to error timeout (evt 20).  Indicates if we were using a password
//      enough time has passed where we should have been disconnected.  This
//      timeout should be hit before we can determine the connection was
//      successful.

static int nl_connect_wait_cb(struct nl_msg *msg, void *arg) {
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct beepnl_cb_args *cb_args = (struct beepnl_cb_args *)arg;
    struct connect_wait_cb_priv *priv =
            (struct connect_wait_cb_priv *)cb_args->priv;
    struct genlmsghdr *gnl_hdr = nlmsg_data(nlmsg_hdr(msg));

    LOG_INFO(log_beep_main, "got nl80211 event: %d", gnl_hdr->cmd);

    priv->last_event = gnl_hdr->cmd;

    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnl_hdr, 0),
          genlmsg_attrlen(gnl_hdr, 0), NULL);

    switch (gnl_hdr->cmd) {
    case NL80211_CMD_DEL_STATION:
        // Don't exit out of the loop in ws_nl_sendmsg yet, deauthenticate
        // and disconnect will have the reason codes.
        priv->status = WS_CONNECT_STATUS_DEL_STATION;
        priv->reason = WS_CONNECT_REASON_UNKNOWN;
        break;

    case NL80211_CMD_DISCONNECT:
        // Only disconnect has NL80211_ATTR_DISCONNECTED_BY_AP, and will
        // typically come after DEAUTHENTICATE so wait until this event
        // to exit the loop, but set the reason for both events.
        if (tb[NL80211_ATTR_DISCONNECTED_BY_AP]) {
            priv->reason_by_ap = true;
        }
        cb_args->status = 0;
        // Fall through.

    case NL80211_CMD_DEAUTHENTICATE:
        if (tb[NL80211_ATTR_REASON_CODE]) {
            priv->reason = (int)nla_get_u16(tb[NL80211_ATTR_REASON_CODE]);
        }
        //LOG_INFO(log_beep_main, "dis/deauth status: %d reason: %d",
        //        priv->status, priv->reason);
        break;

    case NL80211_CMD_NEW_STATION:
        if (priv->wait == WS_CONNECT_WAIT_NEW_STATION) {
            priv->wait = WS_CONNECT_WAIT_CONNECT;
            cb_args->status = 0;
        }
        break;

    case NL80211_CMD_CONNECT:
        if (priv->wait == WS_CONNECT_WAIT_CONNECT) {
            // Ideally this function should not be called after this case
            // is hit.  If we do the connection failed or is happening out
            // of order.
            priv->wait = WS_CONNECT_WAIT_DEL_STATION;
            cb_args->status = 0;
        }
        break;

    case NL80211_CMD_AUTHENTICATE:
    case NL80211_CMD_ASSOCIATE:
        if (tb[NL80211_ATTR_FRAME]) {
            uint8_t *frame = nla_data(tb[NL80211_ATTR_FRAME]);
            // Table 8-1 IEEE std 802.11-2012 for frame[0].
            // See struct ieee80211_mgmt in ieee80211.h for offsets.
            // All values are little endian.
            switch (frame[0] & 0xfc) {
                case 0x10:  // Associate response.
                    priv->status = (int)((uint16_t)((frame[27] << 8) + frame[26]));
                    break;
                case 0xb0:  // Authentication response.
                    priv->status = (int)((uint16_t)((frame[29] << 8) + frame[28]));
                    break;
                default:
                    break;
            }
        } else if (tb[NL80211_ATTR_TIMED_OUT]) {
            priv->status = WS_CONNECT_STATUS_NL_TIMEOUT;
        } else {
            priv->status = WS_CONNECT_STATUS_UNKNOWN;
        }
        //LOG_INFO(log_beep_main, "auth/assoc status: %d", priv->status);

        // If the frame attr was missing or status at this point is not
        // 0, exit out of the loop.
        if (priv->status)
            cb_args->status = 0;
        break;

    default:
        break;
    }

    return NL_SKIP;
}

int ws_netbe_connect(const AP *ap, const char *key, uint8_t return_on,
        int *reason, char **reason_str) {
    struct beepnl_sendmsg_cb sendmsg_cb = {};
    struct connect_wait_cb_priv connect_wait_priv = {};
    int ret;
    int i = 0;

    connect_wait_priv.status = WS_CONNECT_STATUS_UNKNOWN;
    connect_wait_priv.reason = WS_CONNECT_REASON_UNKNOWN;

    // Let  ws_netbe_ctrl_set_client_ap check if ap and key are valid.
    ret = ws_netbe_ctrl_set_client_ap(ap, key);
    if (ret) {
        LOG_ERROR(log_beep_main, "setting client ap info: %d", ret);
        return ret;
    }

    ret = ws_netbe_ctrl_set_mode(BE_MODE_CLIENT);
    if (ret) {
        LOG_ERROR(log_beep_main, "setting client mode: %d", ret);
        return ret;
    }

    ret = ws_netbe_ctrl_net_restart();
    if (ret) {
        LOG_ERROR(log_beep_main, "restarting network: %d", ret);
        return ret;
    }

    // Don't care about the result so we're done.
    if (return_on == CONN_RETURN_ON_NEVER) {
        return 0;
    }

    ret = beepnl_refresh(state);
    if (ret) {
        LOG_ERROR(log_beep_main, "refreshing network: %d", ret);
        return ret;
    }

    // See event_subscribe for callback settings.  Can't use event_subscribe
    // since we want to have multiple timeouts but want to be su
    sendmsg_cb.fin_cb = BEEPNL_DISABLE_CB;
    sendmsg_cb.ack_cb = BEEPNL_DISABLE_CB;
    sendmsg_cb.err_cb = BEEPNL_DISABLE_CB;
    sendmsg_cb.seq_cb = beepnl_seq_check_ignore_cb;
    sendmsg_cb.valid_cb = nl_connect_wait_cb;

    // Subscribe to all events with nl_sock_nonblock.  Can't use
    // event_subscribe since we want to have multiple timeouts but want to be
    // listening to the events the entire time.  Since the callback is only
    // called when a message is present the logic can't be added there.
    ret = beepnl_add_membership(state, true);

    // Exit the loop if we timeout early.
    for (i = 0; (i < CONNECT_TIMEOUT_COUNT) && !ret; i++) {
        sendmsg_cb.timeout = ws_config->conn_timeout[i];
        ret = beepnl_sendmsg(state, NULL, &sendmsg_cb, &connect_wait_priv);
    }

    beepnl_drop_membership(state, true);

    // The last timeout is actually waiting for an error after connect.  In
    // that case if there's no error in status we must have connected.
    // ws_nl_sendmsg will always return 1 on timeout.
    if (i == CONNECT_TIMEOUT_COUNT
            && ret == 1
            && connect_wait_priv.status == 0
            && connect_wait_priv.wait == WS_CONNECT_WAIT_DEL_STATION) {
        *reason = 0;
        *reason_str = strdup("success");
    } else {
        // If NL80211_CMD_DEL_STATION event occured use reason.  If not
        // use status.  Just combine status and reason into argument since
        // upper layers don't care.
        if (connect_wait_priv.status == WS_CONNECT_STATUS_DEL_STATION) {
            *reason = connect_wait_priv.reason;
            if (connect_wait_priv.reason < 0) {
                *reason_str = strdup("unknown disconnect");
            } else {
                // TODO: Do something with reason_by_ap.
                *reason_str = strdup(
                        beepnl_reasonstr(connect_wait_priv.reason));
            }
        } else {
            *reason = connect_wait_priv.status;
            if (connect_wait_priv.status == WS_CONNECT_STATUS_NL_TIMEOUT) {
                // TODO: Figure out if it was an assoc or auth timeout.
                *reason_str = strdup("assoc/auth timeout");
            } else if (connect_wait_priv.status < 0) {
                *reason_str = strdup("unknown assoc/auth error");
            } else {
                *reason_str = strdup(
                        beepnl_statusstr(connect_wait_priv.status));
            }
        }
    }

    // Switch back to setup mode if return_on == ALWAYS or connect
    // was not successful.  The NEVER case is covered above, and
    // the connection must be successful to even check for the
    // CONFIRM_ERROR case.
    if (return_on == CONN_RETURN_ON_ALWAYS || *reason != 0) {
        ret = ws_netbe_ctrl_set_mode(BE_MODE_AP_SETUP);
        if (ret) {
            LOG_ERROR(log_beep_main, "setting setup mode: %d", ret);
            return ret;
        }

        ret = ws_netbe_ctrl_net_restart();
        if (ret) {
            LOG_ERROR(log_beep_main, "restarting network: %d", ret);
            return ret;
        }

        ret = beepnl_refresh(state);
        if (ret) {
            LOG_ERROR(log_beep_main, "refreshing network: %d", ret);
            return ret;
        }
    } else {
        ret = 0;
    }

    return ret;
}

int ws_netbe_commit(void) {
    return ws_netbe_ctrl_commit();
}

int ws_netbe_revert(void) {
    return ws_netbe_ctrl_revert();
}
