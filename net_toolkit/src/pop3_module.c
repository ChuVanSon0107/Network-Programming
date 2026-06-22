#include "pop3_module.h"
#include "socket_utils.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define CMD_SIZE 1024
#define LINE_SIZE 4096
#define BUFFER_SIZE 4096
#define TIMEOUT_SECONDS 5

/* Connect to POP3 server and return socket descriptor. */
static int connect_to_server(const char *server_ip, int server_port) {
    int sockfd;
    struct sockaddr_in server_addr;
    struct timeval timeout;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    /* Set timeout for send and receive operations. */
    timeout.tv_sec = TIMEOUT_SECONDS;
    timeout.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) < 0 ||
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO,
                   &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt");
        close(sockfd);
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)server_port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "[ERROR] Invalid POP3 server IP: %s\n", server_ip);
        close(sockfd);
        return -1;
    }

    if (connect_with_timeout(sockfd, (struct sockaddr *)&server_addr,
                             sizeof(server_addr), TIMEOUT_SECONDS) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

/* Send the complete buffer to POP3 server. */
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

/* Receive one line ending with '\n'. */
static int recv_line(int sockfd, char *buffer, size_t buffer_size) {
    size_t index = 0;
    char character;
    ssize_t received;

    while (index < buffer_size - 1) {
        received = recv(sockfd, &character, 1, 0);

        if (received < 0 && errno == EINTR) {
            continue;
        }

        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(stderr,
                        "[ERROR] Timeout while waiting for POP3 response\n");
            } else {
                perror("recv");
            }
            return -1;
        }

        if (received == 0) {
            if (index == 0) {
                return -1;
            }
            break;
        }

        buffer[index++] = character;
        if (character == '\n') {
            break;
        }
    }

    buffer[index] = '\0';
    return (int)index;
}

/* Check whether a POP3 response begins with +OK. */
static int check_pop3_ok(const char *response) {
    return strncmp(response, "+OK", 3) == 0;
}

/* Send one POP3 command and check its first response line. */
static int send_pop3_command(int sockfd, const char *command,
                             char *response, size_t response_size) {
    printf("[CLIENT] %s", command);

    if (send_all(sockfd, command) < 0) {
        return -1;
    }

    if (recv_line(sockfd, response, response_size) < 0) {
        return -1;
    }

    printf("[SERVER] %s", response);

    if (!check_pop3_ok(response)) {
        fprintf(stderr, "[ERROR] POP3 command failed: %s", response);
        return -1;
    }

    return 0;
}

/* Read LIST multi-line response until a line containing only one dot. */
static int read_multiline_response(int sockfd) {
    char line[LINE_SIZE];

    while (1) {
        if (recv_line(sockfd, line, sizeof(line)) < 0) {
            fprintf(stderr, "[ERROR] Cannot read multi-line response\n");
            return -1;
        }

        printf("%s", line);

        if (strcmp(line, ".\r\n") == 0 ||
            strcmp(line, ".\n") == 0 ||
            strcmp(line, ".") == 0) {
            break;
        }
    }

    return 0;
}

/* Read one RETR response and check its Subject header. */
static int read_email_and_check_subject(int sockfd, const char *subject) {
    char line[LINE_SIZE];
    char expected_subject[CMD_SIZE];
    int found = 0;

    if (snprintf(expected_subject, sizeof(expected_subject),
                 "Subject: %s", subject) >= (int)sizeof(expected_subject)) {
        fprintf(stderr, "[ERROR] Subject is too long\n");
        return -1;
    }

    while (1) {
        if (recv_line(sockfd, line, sizeof(line)) < 0) {
            fprintf(stderr, "[ERROR] Cannot read email content\n");
            return -1;
        }

        if (strcmp(line, ".\r\n") == 0 ||
            strcmp(line, ".\n") == 0 ||
            strcmp(line, ".") == 0) {
            break;
        }

        /* Print complete email headers and body to terminal. */
        printf("%s", line);

        /* Remove CRLF before comparing Subject header. */
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, expected_subject) == 0) {
            found = 1;
        }
    }

    return found;
}

int pop3_find_email_by_subject(const char *server_ip, int server_port,
                               const char *username, const char *password,
                               const char *subject) {
    int sockfd = -1;
    int message_count;
    int message_number;
    int email_result;
    int subject_found = 0;
    int logged_in = 0;
    char response[BUFFER_SIZE];
    char command[CMD_SIZE];

    if (server_ip == NULL || username == NULL || password == NULL ||
        subject == NULL) {
        fprintf(stderr, "[ERROR] Invalid POP3 argument\n");
        return -1;
    }

    if (server_port <= 0 || server_port > 65535) {
        fprintf(stderr, "[ERROR] Invalid POP3 server port: %d\n",
                server_port);
        return -1;
    }

    /* 1. Connect to POP3 server. */
    sockfd = connect_to_server(server_ip, server_port);
    if (sockfd < 0) {
        return -1;
    }

    /* 2. Receive greeting beginning with +OK. */
    if (recv_line(sockfd, response, sizeof(response)) < 0) {
        fprintf(stderr, "[ERROR] Did not receive POP3 greeting\n");
        goto cleanup;
    }
    printf("[SERVER] %s", response);

    if (!check_pop3_ok(response)) {
        fprintf(stderr, "[ERROR] POP3 greeting is not +OK\n");
        goto cleanup;
    }

    /* 3. Send USER username. */
    snprintf(command, sizeof(command), "USER %s\r\n", username);
    if (send_pop3_command(sockfd, command, response,
                          sizeof(response)) < 0) {
        goto cleanup;
    }

    /* 4. Send PASS password. */
    snprintf(command, sizeof(command), "PASS %s\r\n", password);
    if (send_pop3_command(sockfd, command, response,
                          sizeof(response)) < 0) {
        goto cleanup;
    }
    logged_in = 1;

    /* 5. Send STAT and get number of messages. */
    if (send_pop3_command(sockfd, "STAT\r\n", response,
                          sizeof(response)) < 0 ||
        sscanf(response, "+OK %d", &message_count) != 1) {
        fprintf(stderr, "[ERROR] Cannot parse POP3 STAT response\n");
        goto cleanup;
    }

    /* 6. Send LIST and read its multi-line response. */
    if (send_pop3_command(sockfd, "LIST\r\n", response,
                          sizeof(response)) < 0 ||
        read_multiline_response(sockfd) < 0) {
        goto cleanup;
    }

    /* 7. Retrieve messages from newest to oldest. */
    for (message_number = message_count;
         message_number >= 1 && !subject_found;
         message_number--) {
        snprintf(command, sizeof(command), "RETR %d\r\n", message_number);

        if (send_pop3_command(sockfd, command, response,
                              sizeof(response)) < 0) {
            continue;
        }

        printf("\n--- Content of Email #%d ---\n", message_number);
        email_result = read_email_and_check_subject(sockfd, subject);
        printf("--- End of Email #%d ---\n\n", message_number);
        if (email_result < 0) {
            subject_found = 0;
            goto cleanup;
        }
        subject_found = email_result;
    }

cleanup:
    /* 8. Send QUIT after a successful login. */
    if (logged_in) {
        if (send_pop3_command(sockfd, "QUIT\r\n", response,
                              sizeof(response)) < 0) {
            subject_found = 0;
        }
    }
    close(sockfd);

    if (!subject_found) {
        fprintf(stderr, "[ERROR] Email subject '%s' was not found\n", subject);
        return -1;
    }

    printf("[POP3] Found email with subject: %s\n", subject);
    return 0;
}
