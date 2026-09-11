#include <assert.h>
#include <stdio.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "beep/debug.h"
#include "beepnl/beepnl.h"

struct beepnl_state *state = NULL;

struct nla_policy sta_info_policy[NL80211_STA_INFO_MAX + 1] = {
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
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct nlattr *sta_info[NL80211_STA_INFO_MAX + 1];
    struct genlmsghdr *gnl_hdr = nlmsg_data(nlmsg_hdr(msg));
    struct beepnl_cb_args *cb_args = (struct beepnl_cb_args *)arg;

    int8_t *sig_avg = (int8_t *)cb_args->priv;

    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnl_hdr, 0),
          genlmsg_attrlen(gnl_hdr, 0), NULL);

    if (!tb[NL80211_ATTR_STA_INFO]) {
        LOG_ERROR(log_beep_main, "sta_info missing");
        return NL_SKIP;
    }

    if (nla_parse_nested(sta_info, NL80211_STA_INFO_MAX,
            tb[NL80211_ATTR_STA_INFO], sta_info_policy)) {
        LOG_ERROR(log_beep_main, "failed to parse sta_info");
        return NL_SKIP;
    }

    if (sta_info[NL80211_STA_INFO_SIGNAL_AVG]) {
        *sig_avg = nla_get_u8(sta_info[NL80211_STA_INFO_SIGNAL_AVG]);
    } else {
        *sig_avg = 0;
    }

    return NL_SKIP;
}

static int lua_beepnl_get_sig_avg(lua_State *L) {
    struct nl_msg *msg = NULL;
    struct beepnl_sendmsg_cb sendmsg_cb = {0};
    int8_t sig_avg = 0;

    msg = nlmsg_alloc();
    assert(msg);

    genlmsg_put(msg, 0, 0, state->nl80211_id, 0, NLM_F_DUMP,
            NL80211_CMD_GET_STATION, 0);
    NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, state->if_index);

    sendmsg_cb.valid_cb = nl_get_station_cb;

    if(beepnl_sendmsg(state, msg, &sendmsg_cb, &sig_avg)) {
        LOG_ERROR(log_beep_main, "sendmsg failed");
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, sig_avg);
    }

    return 1;

nla_put_failure:
    LOG_ERROR(log_beep_main, "nla put failure");
    lua_pushnil(L);

    return 1;
}

static int lua_beepnl_init(lua_State *L) {
    if(!state) {
        state = beepnl_init("wlan0");
    }

    // TODO: This is a workaround for virtual devices, which may or may not
    // have a wlan0 interface.  Subsequent calls to get_sig_avg will always
    // return 0.
    if(!state) {
        state = beepnl_init("eth0");
    }

    lua_pushboolean(L, state != NULL ? 1 : 0);

    return 1;
}

static int lua_beepnl_refresh(lua_State *L) {
    if(!state) {
        lua_pushboolean(L, 0);
        return 1;
    }

    beepnl_refresh(state);

    lua_pushboolean(L, 1);
    return 1;
}

static int lua_beepnl_free(lua_State *L) {
    if(state) {
        beepnl_free(state);
        state = NULL;
    }

    lua_pushboolean(L, 1);

    return 1;
}

static const luaL_Reg beepnl_reg[] = {
    {"init", lua_beepnl_init},
    {"refresh", lua_beepnl_refresh},
    {"free", lua_beepnl_free},
    {"get_sig_avg", lua_beepnl_get_sig_avg},
    {NULL, NULL}
};

int luaopen_beepnl(lua_State *L) {
    log_beep_main = LOG_CATEGORY_GET("beepnl.so");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    luaL_openlib(L, "beepnl", beepnl_reg, 0);

    return 1;
}

int luaclose_beepnl(lua_State *L) {
    return 1;
}
