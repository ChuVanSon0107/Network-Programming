#include "ftp_module.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define BUFFER_SIZE 4096
#define COMMAND_SIZE 1024
#define IP_SIZE 16
#define TIMEOUT_SECONDS 5

/* Set timeout for control socket and data socket. */
static int set_socket_timeout(int sockfd) {
    struct timeval timeout;

    timeout.tv_sec = TIMEOUT_SECONDS;
    timeout.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) < 0 ||
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO,
                   &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt");
        return -1;
    }

    return 0;
}

/* Send command to FTP server through control connection. */
static int send_command(int sockfd, const char *command) {
    size_t command_length;
    ssize_t sent;

    printf(">> Client request: %s", command);
    command_length = strlen(command);
    sent = write(sockfd, command, command_length);

    if (sent < 0) {
        perror("write");
        return -1;
    }

    if ((size_t)sent != command_length) {
        fprintf(stderr, "Failed to send complete FTP command\n");
        return -1;
    }

    return 0;
}

/* Receive one command response from FTP server. */
static int receive_response(int sockfd, char *response,
                            size_t response_size) {
    char buffer[BUFFER_SIZE];
    int total_received = 0;
    int received;

    while (total_received < (int)sizeof(buffer) - 1) {
        received = (int)read(sockfd, buffer + total_received,
                             sizeof(buffer) - (size_t)total_received - 1);
        if (received < 0) {
            perror("read");
            return -1;
        }

        if (received == 0) {
            break;
        }

        total_received += received;
        buffer[total_received] = '\0';

        if (strstr(buffer, "\r\n") != NULL) {
            break;
        }
    }

    if (total_received == 0) {
        fprintf(stderr, "FTP server closed the connection\n");
        return -1;
    }

    printf("<< Server response: %s", buffer);

    if (response != NULL && response_size > 0) {
        snprintf(response, response_size, "%s", buffer);
    }

    return atoi(buffer);
}

/* Open data connection using IP and port returned by PASV. */
static int open_data_connection(const char *ip, int port) {
    int data_sock;
    struct sockaddr_in data_addr;

    data_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (data_sock < 0) {
        perror("socket");
        return -1;
    }

    if (set_socket_timeout(data_sock) < 0) {
        close(data_sock);
        return -1;
    }

    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    data_addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, ip, &data_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(data_sock);
        return -1;
    }

    if (connect(data_sock, (struct sockaddr *)&data_addr,
                sizeof(data_addr)) < 0) {
        perror("connect data socket");
        close(data_sock);
        return -1;
    }

    return data_sock;
}

int ftp_connect_to_server(const char *server_ip, int server_port) {
    int control_sock;
    int response_code;
    struct sockaddr_in server_addr;

    /* Create control socket. */
    control_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (control_sock < 0) {
        perror("socket");
        return -1;
    }

    if (set_socket_timeout(control_sock) < 0) {
        close(control_sock);
        return -1;
    }

    /* Set up server address. */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)server_port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(control_sock);
        return -1;
    }

    /* Connect to server through control connection. */
    if (connect(control_sock, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        perror("connect");
        close(control_sock);
        return -1;
    }

    /* Receive greeting from FTP server. */
    response_code = receive_response(control_sock, NULL, 0);
    if (response_code != 220) {
        fprintf(stderr, "FTP greeting is not 220\n");
        close(control_sock);
        return -1;
    }

    return control_sock;
}

int ftp_login(int control_sock, const char *username,
              const char *password) {
    char command[COMMAND_SIZE];
    int response_code;

    /* Send USER username. */
    snprintf(command, sizeof(command), "USER %s\r\n", username);
    if (send_command(control_sock, command) < 0) {
        return -1;
    }

    response_code = receive_response(control_sock, NULL, 0);
    if (response_code != 331 && response_code != 230) {
        fprintf(stderr, "FTP USER command failed\n");
        return -1;
    }

    /* Send PASS password when server returns 331. */
    if (response_code == 331) {
        snprintf(command, sizeof(command), "PASS %s\r\n", password);
        if (send_command(control_sock, command) < 0 ||
            receive_response(control_sock, NULL, 0) != 230) {
            fprintf(stderr, "FTP PASS command failed\n");
            return -1;
        }
    }

    /* Send PWD. */
    if (send_command(control_sock, "PWD\r\n") < 0 ||
        receive_response(control_sock, NULL, 0) != 257) {
        fprintf(stderr, "FTP PWD command failed\n");
        return -1;
    }

    return 0;
}

int ftp_enter_passive_mode(int control_sock, char *data_ip,
                           size_t data_ip_size, int *data_port) {
    char response[BUFFER_SIZE];
    char *start;
    int h1, h2, h3, h4, p1, p2;

    /* Send PASV. */
    if (send_command(control_sock, "PASV\r\n") < 0) {
        return -1;
    }

    /* Receive PASV response. */
    if (receive_response(control_sock, response, sizeof(response)) != 227) {
        fprintf(stderr, "FTP PASV command failed\n");
        return -1;
    }

    /* Parse h1,h2,h3,h4,p1,p2 from response. */
    start = strchr(response, '(');
    if (start == NULL ||
        sscanf(start + 1, "%d,%d,%d,%d,%d,%d",
               &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        fprintf(stderr, "Server response is not formatted correctly\n");
        return -1;
    }

    if ((unsigned)h1 > 255 || (unsigned)h2 > 255 ||
        (unsigned)h3 > 255 || (unsigned)h4 > 255 ||
        (unsigned)p1 > 255 || (unsigned)p2 > 255) {
        fprintf(stderr, "Invalid IP or port in PASV response\n");
        return -1;
    }

    if (snprintf(data_ip, data_ip_size, "%d.%d.%d.%d",
                 h1, h2, h3, h4) >= (int)data_ip_size) {
        fprintf(stderr, "Data IP buffer is too small\n");
        return -1;
    }

    *data_port = p1 * 256 + p2;
    return 0;
}

int ftp_list_files(int control_sock) {
    char buffer[BUFFER_SIZE];
    char data_ip[IP_SIZE];
    int data_port;
    int data_sock;
    int response_code;
    int received;

    /* Send PASV and get data address. */
    if (ftp_enter_passive_mode(control_sock, data_ip,
                               sizeof(data_ip), &data_port) < 0) {
        return -1;
    }

    /* Open data connection. */
    data_sock = open_data_connection(data_ip, data_port);
    if (data_sock < 0) {
        return -1;
    }

    /* Send LIST command. */
    if (send_command(control_sock, "LIST\r\n") < 0) {
        close(data_sock);
        return -1;
    }

    response_code = receive_response(control_sock, NULL, 0);
    if (response_code != 125 && response_code != 150) {
        fprintf(stderr, "FTP LIST command failed\n");
        close(data_sock);
        return -1;
    }

    /* Receive and print file list through data connection. */
    while ((received = (int)read(data_sock, buffer,
                                 sizeof(buffer) - 1)) > 0) {
        buffer[received] = '\0';
        printf("%s", buffer);
    }
    close(data_sock);

    if (received < 0) {
        perror("read data socket");
        return -1;
    }

    /* Receive 226 after data connection is closed. */
    if (receive_response(control_sock, NULL, 0) != 226) {
        fprintf(stderr, "FTP LIST transfer did not complete\n");
        return -1;
    }

    return 0;
}

int ftp_download_file(int control_sock, const char *remote_file,
                      const char *local_file) {
    char buffer[BUFFER_SIZE];
    char data_ip[IP_SIZE];
    char command[COMMAND_SIZE];
    int data_port;
    int data_sock;
    int response_code;
    int received;
    FILE *file;

    /* Send PASV and get data address. */
    if (ftp_enter_passive_mode(control_sock, data_ip,
                               sizeof(data_ip), &data_port) < 0) {
        return -1;
    }

    /* Open data connection. */
    data_sock = open_data_connection(data_ip, data_port);
    if (data_sock < 0) {
        return -1;
    }

    /* Send RETR remote_file. */
    snprintf(command, sizeof(command), "RETR %s\r\n", remote_file);
    if (send_command(control_sock, command) < 0) {
        close(data_sock);
        return -1;
    }

    response_code = receive_response(control_sock, NULL, 0);
    if (response_code != 125 && response_code != 150) {
        fprintf(stderr, "FTP RETR command failed\n");
        close(data_sock);
        return -1;
    }

    /* Open local file. */
    file = fopen(local_file, "wb");
    if (file == NULL) {
        perror("fopen");
        close(data_sock);
        return -1;
    }

    /* Receive file through data connection. */
    while ((received = (int)read(data_sock, buffer,
                                 sizeof(buffer))) > 0) {
        if (fwrite(buffer, 1, (size_t)received, file) !=
            (size_t)received) {
            perror("fwrite");
            fclose(file);
            close(data_sock);
            return -1;
        }
    }

    fclose(file);
    close(data_sock);

    if (received < 0) {
        perror("read data socket");
        return -1;
    }

    /* Receive 226 after data connection is closed. */
    if (receive_response(control_sock, NULL, 0) != 226) {
        fprintf(stderr, "FTP RETR transfer did not complete\n");
        return -1;
    }

    return 0;
}

int ftp_quit(int control_sock) {
    int response_code;

    if (send_command(control_sock, "QUIT\r\n") < 0) {
        close(control_sock);
        return -1;
    }

    response_code = receive_response(control_sock, NULL, 0);
    close(control_sock);

    return response_code == 221 ? 0 : -1;
}
