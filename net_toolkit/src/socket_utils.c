#include "socket_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

int connect_with_timeout(int sockfd, const struct sockaddr *address,
                         socklen_t address_length, int timeout_seconds) {
    int original_flags;
    int socket_error;
    socklen_t error_length = sizeof(socket_error);
    fd_set write_fds;
    struct timeval timeout;
    int result;

    original_flags = fcntl(sockfd, F_GETFL, 0);
    if (original_flags < 0 ||
        fcntl(sockfd, F_SETFL, original_flags | O_NONBLOCK) < 0) {
        return -1;
    }

    result = connect(sockfd, address, address_length);
    if (result == 0) {
        fcntl(sockfd, F_SETFL, original_flags);
        return 0;
    }

    if (errno != EINPROGRESS) {
        fcntl(sockfd, F_SETFL, original_flags);
        return -1;
    }

    FD_ZERO(&write_fds);
    FD_SET(sockfd, &write_fds);
    timeout.tv_sec = timeout_seconds;
    timeout.tv_usec = 0;

    do {
        result = select(sockfd + 1, NULL, &write_fds, NULL, &timeout);
    } while (result < 0 && errno == EINTR);

    if (result == 0) {
        errno = ETIMEDOUT;
        fcntl(sockfd, F_SETFL, original_flags);
        return -1;
    }

    if (result < 0 ||
        getsockopt(sockfd, SOL_SOCKET, SO_ERROR,
                   &socket_error, &error_length) < 0) {
        fcntl(sockfd, F_SETFL, original_flags);
        return -1;
    }

    fcntl(sockfd, F_SETFL, original_flags);
    if (socket_error != 0) {
        errno = socket_error;
        return -1;
    }

    return 0;
}
