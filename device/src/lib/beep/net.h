#ifndef _BEEP_NET
#define _BEEP_NET

#include <pthread.h>
#include <stdint.h>

#include "beeplib.h"

// Half a meg for buffers. Probably not necessary.
#define MAX_CONNECTIONS 16
#define BEEP_CONN_BUF_SIZE 32768

#define POLLFD_INDEX_LISTEN 0

typedef enum {
    BEEP_CONN_VALID,
    BEEP_CONN_RESPONSE_FULL
} ConnCode;

typedef struct {
    ConnCode code;
    uint8_t buffer[BEEP_CONN_BUF_SIZE];
    int cursor;
    uint8_t response_buffer[BEEP_CONN_BUF_SIZE];
    int response_cursor;
} ConnState;

// Holds struct pollfds (used by poll()) and connection states.
// pollfds contains the listen socket, as well as any active connection
//     sockets.
// conn_states contains states for each active connection.
// conn_states has one more element than pollfds.
typedef struct {
    int port_num;
    BeepStaticVector* pollfds;     // Contains struct poll_fd
    BeepStaticVector* conn_states;  // Contains ConnState*
    pthread_mutex_t mutex;
} BeepPortState;

typedef struct {
    struct pollfd* pollfd;
    ConnState* state;
} BeepConnection;

// May only be called from within a request callback (not checked)
void beep_respond(BeepConnection* conn, uint8_t* buf, int len);

// May be called from anywhere
void beep_broadcast(BeepPortState* port_state, uint8_t* buf, int len);

typedef int (*BeepSocketIncomingCB)(BeepConnection* conn,
                                     void* userdata);

BeepPortState* beep_register_port(int port_num,
                                  int num_connections,
                                  BeepSocketIncomingCB callback,
                                  void* userdata);

// These three may only be called from within a request callback (not checked)
int beep_net_read(BeepConnection* conn, uint8_t** buf, int len);
int beep_net_read_message(BeepConnection* conn, uint8_t** buf, int* len);
void beep_net_write_message(BeepConnection* conn, uint8_t* buf, int len);

#endif  // _BEEP_NET
