#include "smtp_module.h"
#include "socket_utils.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define BUFFER_SIZE 4096
#define CMD_SIZE 1024
#define TIMEOUT_SECONDS 5

/* Connect to SMTP server and return socket descriptor. */
static int connect_to_server(const char *server_ip, int server_port) {
    int sockfd;
    struct sockaddr_in server_addr;
    struct timeval timeout;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("[ERROR] socket");
        return -1;
    }

    /* Set timeout for send and receive operations. */
    timeout.tv_sec = TIMEOUT_SECONDS;
    timeout.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) < 0 ||
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO,
                   &timeout, sizeof(timeout)) < 0) {
        perror("[ERROR] setsockopt");
        close(sockfd);
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)server_port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "[ERROR] Invalid SMTP server IP: %s\n", server_ip);
        close(sockfd);
        return -1;
    }

    if (connect_with_timeout(sockfd, (struct sockaddr *)&server_addr,
                             sizeof(server_addr), TIMEOUT_SECONDS) < 0) {
        perror("[ERROR] connect");
        close(sockfd);
        return -1;
    }

    printf("[INFO] Connected to %s:%d\n", server_ip, server_port);
    return sockfd;
}

/* Send buffer all */
static int send_all(int sockfd, const char *buffer) {
    size_t total = strlen(buffer);
    size_t sent = 0;
    while (sent < total) {
        ssize_t n = send(sockfd, buffer + sent, total - sent, 0);

        if (n < 0 && errno == EINTR) {
            continue;
        }

        if (n <= 0) {
            perror("[ERROR] send");
            return -1;
        }

        sent += (size_t)n;
    }

    return 0;
}

/* Receive response from SMTP server */
static int recv_response(int sockfd, char *buffer, size_t buffer_size) {
    ssize_t received;
    memset(buffer, 0, buffer_size);
    received = recv(sockfd, buffer, buffer_size - 1, 0);

    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            fprintf(stderr,
                    "[ERROR] Timeout while waiting for SMTP response\n");
        } else {
            perror("[ERROR] recv");
        }
        return -1;
    }

    if (received == 0) {
        fprintf(stderr, "[ERROR] SMTP server closed connection suddenly\n");
        return -1;
    }

    buffer[received] = '\0';
    printf("[SERVER] %s", buffer);
    return 0;
}

/* Check whether response begins with expected SMTP code. */
static int check_response_code(const char *response,
                               const char *expected_code) {
    return strncmp(response, expected_code, 3) == 0;
}

/* Send an SMTP command, receive response and check its code. */
static int send_smtp_command(int sockfd, const char *command,
                             const char *expected_code) {
    char response[BUFFER_SIZE];
    printf("[CLIENT] %s", command);

    if (send_all(sockfd, command) < 0 ||
        recv_response(sockfd, response, sizeof(response)) < 0) {
        return -1;
    }

    if (!check_response_code(response, expected_code)) {
        fprintf(stderr, "[ERROR] Unexpected SMTP response: %s", response);
        return -1;
    }

    return 0;
}

int smtp_send_report(const char *server_ip, int server_port,
                     const char *sender, const char *recipient,
                     const char *subject, const char *body) {
    int sockfd = -1;
    int result = -1;
    char response[BUFFER_SIZE];
    char command[CMD_SIZE];
    size_t body_length;
    
    if (server_ip == NULL || sender == NULL || recipient == NULL ||
        subject == NULL || body == NULL) {
        fprintf(stderr, "[ERROR] Invalid SMTP argument\n");
        return -1;
    }

    if (server_port <= 0 || server_port > 65535) {
        fprintf(stderr, "[ERROR] Invalid SMTP server port: %d\n",
                server_port);
        return -1;
    }

    /* 1. Create socket and connect to SMTP server. */
    sockfd = connect_to_server(server_ip, server_port);
    if (sockfd < 0) {
        return -1;
    }

    /* 2. Receive greeting 220 from server. */
    if (recv_response(sockfd, response, sizeof(response)) < 0 ||
        !check_response_code(response, "220")) {
        fprintf(stderr, "[ERROR] SMTP greeting is not 220\n");
        goto cleanup;
    }

    /* 3. Send HELO net-toolkit.local. */
    if (send_smtp_command(sockfd, "HELO net-toolkit.local\r\n",
                          "250") < 0) {
        goto cleanup;
    }

    /* 4. Send MAIL FROM. */
    snprintf(command, sizeof(command), "MAIL FROM:<%s>\r\n", sender);
    if (send_smtp_command(sockfd, command, "250") < 0) {
        goto cleanup;
    }

    /* 5. Send RCPT TO. */
    snprintf(command, sizeof(command), "RCPT TO:<%s>\r\n", recipient);
    if (send_smtp_command(sockfd, command, "250") < 0) {
        goto cleanup;
    }

    /* 6. Send DATA and expect 354. */
    if (send_smtp_command(sockfd, "DATA\r\n", "354") < 0) {
        goto cleanup;
    }

    /* 7. Send email headers. */
    snprintf(command, sizeof(command), "From: %s\r\n", sender);
    if (send_all(sockfd, command) < 0) {
        goto cleanup;
    }

    snprintf(command, sizeof(command), "To: %s\r\n", recipient);
    if (send_all(sockfd, command) < 0) {
        goto cleanup;
    }

    snprintf(command, sizeof(command), "Subject: %s\r\n", subject);
    if (send_all(sockfd, command) < 0 ||
        send_all(sockfd, "\r\n") < 0) {
        goto cleanup;
    }

    /* 8. Send report body. */
    if (send_all(sockfd, body) < 0) {
        goto cleanup;
    }

    /* 9. End DATA with a line containing only one dot. */
    body_length = strlen(body);
    if (body_length >= 2 &&
        strcmp(body + body_length - 2, "\r\n") == 0) {
        if (send_all(sockfd, ".\r\n") < 0) {
            goto cleanup;
        }
    } else {
        if (send_all(sockfd, "\r\n.\r\n") < 0) {
            goto cleanup;
        }
    }

    if (recv_response(sockfd, response, sizeof(response)) < 0 ||
        !check_response_code(response, "250")) {
        fprintf(stderr, "[ERROR] SMTP server rejected report content\n");
        goto cleanup;
    }

    /* 10. Send QUIT. */
    if (send_smtp_command(sockfd, "QUIT\r\n", "221") < 0) {
        goto cleanup;
    }

    result = 0;

cleanup:
    close(sockfd);
    return result;
}
