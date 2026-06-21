#include "http_module.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define BUFFER_SIZE 4096
#define REQUEST_SIZE 1024
#define RESPONSE_SIZE 16384
#define HTTP_TIMEOUT_SECONDS 5

int http_connect_to_server(const char *server_ip, int server_port) {
    int sockfd;
    struct sockaddr_in server_addr;
    struct timeval timeout;

    /* Create TCP socket. */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("[ERROR] socket");
        return -1;
    }

    /* Set timeout so the toolkit does not wait forever. */
    timeout.tv_sec = HTTP_TIMEOUT_SECONDS;
    timeout.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) < 0 ||
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO,
                   &timeout, sizeof(timeout)) < 0) {
        perror("[ERROR] setsockopt");
        close(sockfd);
        return -1;
    }

    /* Set up server address. */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)server_port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "[ERROR] Invalid HTTP server IP: %s\n", server_ip);
        close(sockfd);
        return -1;
    }

    /* Connect to HTTP server. */
    if (connect(sockfd, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        perror("[ERROR] HTTP connect");
        close(sockfd);
        return -1;
    }

    printf("[HTTP] Connected to %s:%d\n", server_ip, server_port);
    return sockfd;
}

int http_send_all(int sockfd, const char *buffer, size_t buffer_length) {
    size_t total_sent = 0;

    while (total_sent < buffer_length) {
        ssize_t sent = send(sockfd, buffer + total_sent,
                            buffer_length - total_sent, 0);

        if (sent < 0 && errno == EINTR) {
            continue;
        }

        if (sent < 0) {
            perror("[ERROR] HTTP send");
            return -1;
        }

        if (sent == 0) {
            fprintf(stderr, "[ERROR] HTTP connection closed while sending\n");
            return -1;
        }

        total_sent += (size_t)sent;
    }

    return 0;
}

int http_receive_response(int sockfd, char *response,
                          size_t response_size) {
    char buffer[BUFFER_SIZE];
    size_t total_received = 0;
    ssize_t received;

    if (response == NULL || response_size == 0) {
        return -1;
    }

    while ((received = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
        if (total_received + (size_t)received >= response_size) {
            fprintf(stderr, "[ERROR] HTTP response is too large\n");
            return -1;
        }

        memcpy(response + total_received, buffer, (size_t)received);
        total_received += (size_t)received;
    }

    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            fprintf(stderr,
                    "[ERROR] Timeout while waiting for HTTP response\n");
        } else {
            perror("[ERROR] HTTP recv");
        }
        return -1;
    }

    response[total_received] = '\0';
    printf("[HTTP] Received %zu bytes from server\n", total_received);
    return (int)total_received;
}

int check_http_response(const char *response, const char *expected_body) {
    const char *header_end;
    const char *body;
    const char *status_line_end;
    size_t status_line_length;

    if (response == NULL || expected_body == NULL) {
        return -1;
    }

    /* Check that the status line contains 200 OK. */
    status_line_end = strstr(response, "\r\n");
    if (status_line_end == NULL) {
        fprintf(stderr, "[ERROR] Invalid HTTP status line\n");
        return -1;
    }

    status_line_length = (size_t)(status_line_end - response);
    if (status_line_length < strlen("HTTP/1.0 200 OK") ||
        (strncmp(response, "HTTP/1.1 200 OK", strlen("HTTP/1.1 200 OK")) != 0 &&
         strncmp(response, "HTTP/1.0 200 OK", strlen("HTTP/1.0 200 OK")) != 0)) {
        fprintf(stderr, "[ERROR] HTTP response is not 200 OK\n");
        return -1;
    }

    /* The empty line separates HTTP headers from response body. */
    header_end = strstr(response, "\r\n\r\n");
    if (header_end == NULL) {
        fprintf(stderr, "[ERROR] HTTP response has no header terminator\n");
        return -1;
    }
    body = header_end + 4;

    if (strstr(body, expected_body) == NULL) {
        fprintf(stderr, "[ERROR] HTTP body does not contain '%s'\n",
                expected_body);
        return -1;
    }

    return 0;
}

int http_get_status(const char *server_ip, int server_port,
                    const char *host_header, const char *path) {
    int sockfd = -1;
    int request_length;
    char request[REQUEST_SIZE];
    char response[RESPONSE_SIZE];
    int result = -1;

    if (server_ip == NULL || host_header == NULL || path == NULL) {
        fprintf(stderr, "[ERROR] Invalid HTTP argument\n");
        return -1;
    }

    if (server_port <= 0 || server_port > 65535) {
        fprintf(stderr, "[ERROR] Invalid HTTP server port: %d\n",
                server_port);
        return -1;
    }

    /* 1. Build HTTP GET request. */
    request_length = snprintf(request, sizeof(request),
                              "GET %s HTTP/1.1\r\n"
                              "Host: %s\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              path, host_header);
    if (request_length < 0 || (size_t)request_length >= sizeof(request)) {
        fprintf(stderr, "[ERROR] HTTP request is too long\n");
        return -1;
    }

    /* 2. Connect to HTTP server. */
    sockfd = http_connect_to_server(server_ip, server_port);
    if (sockfd < 0) {
        return -1;
    }

    /* 3. Send request. */
    printf("[CLIENT] %s", request);
    if (http_send_all(sockfd, request, (size_t)request_length) < 0) {
        goto cleanup;
    }

    /* 4. Receive response until Connection: close. */
    if (http_receive_response(sockfd, response, sizeof(response)) < 0) {
        goto cleanup;
    }

    printf("[SERVER] %.80s%s\n", response,
           strlen(response) > 80 ? "..." : "");

    /* 5. Check status line and body content. */
    if (check_http_response(response, "HTTP_SERVICE_OK") < 0) {
        goto cleanup;
    }

    result = 0;

cleanup:
    close(sockfd);
    return result;
}
