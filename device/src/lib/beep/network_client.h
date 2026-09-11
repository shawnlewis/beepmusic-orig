#ifndef BEEP_NET_CLIENT
#define BEEP_NET_CLIENT

#include <json.h>

#include "beep/network.h"

// Currently just used by beepdevio to receive notifaction updates.
// We could move all client stuff into here.
// We'd need to have the notion of different types of channels (json/struct)

typedef void (*OnNotificationCB)(
        int role, int subrole, const char* event, json_object* state);

typedef struct {
    int command_fd;
    int notify_fd;
    OnNotificationCB on_notification_cb;
    int seqs[BEEP_MAX_ROLES][BEEP_MAX_SUBROLES];
} BeepNetworkClient;

BeepNetworkClient* beep_net_client_init(
        const char* ip, OnNotificationCB on_notification_cb);

#endif  // BEEP_NET_CLIENT
