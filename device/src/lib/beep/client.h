/* Beep network client library.
 *
 * *****************************************************************/
#include <stdbool.h>
#include <stdint.h>

#include "beepports.h"

/* Connects to a beep device at the given IP address and port.
 *
 * Returns: socket file descriptor for the connection, or -1 on error.
 */
int beep_connect(const char* ip_addr, unsigned short port);
void beep_disconnect(int socketfd);

/* Sends a command and it's args over a socket, and returns the response.
 *
 * Returns: The response, which is a buffer of bytes.
 */
uint8_t* beep_command(int socketfd, int command_id, const char* args);
uint8_t* beep_command_len(int socketfd, int command_id, uint8_t* args, int largs);

uint8_t* beep_request(
        int socketfd, int target_id, int subtarget_id, int request_id,
        uint8_t* args, int args_len);

/* Send total bytes, over socket. Returns true if successful.
 */
bool beep_send(int socketfd, const uint8_t* buf, int total);

/* Reads a length encoded message from a socket.
 */
uint8_t* beep_get_message(int socketfd);
