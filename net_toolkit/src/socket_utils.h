#ifndef SOCKET_UTILS_H
#define SOCKET_UTILS_H

#include <sys/socket.h>

/* Connect a TCP socket within timeout_seconds.
 * Return 0 on success and -1 on error or timeout.
 */
int connect_with_timeout(int sockfd, const struct sockaddr *address,
                         socklen_t address_length, int timeout_seconds);

#endif
