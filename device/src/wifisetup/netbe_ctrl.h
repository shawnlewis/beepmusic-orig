#ifndef WIFI_SETUP_NETBE_CTRL_H
#define WIFI_SETUP_NETBE_CTRL_H


// These functions are to be used by the network backend (except
// netbe_debug.c) to actually control the state of the network.  This
// allows different control backends other than uci to be used.

#include "wifisetup.h"


void ws_netbe_ctrl_cleanup(void);
int ws_netbe_ctrl_init(void);
int ws_netbe_ctrl_net_restart(void);
BEMode ws_netbe_ctrl_get_mode(void);
int ws_netbe_ctrl_set_mode(BEMode);
int ws_netbe_ctrl_set_client_ap(const AP *ap, const char *key);
int ws_netbe_ctrl_commit(void);
int ws_netbe_ctrl_revert(void);


#endif  // WIFI_SETUP_NETBE_CTRL_H
