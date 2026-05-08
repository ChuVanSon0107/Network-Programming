#include <stdio.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define BUFFER_SIZE 1024
#define CONTROL_PORT 21
#define DATA_PORT 20
#define IP_SIZE 16

// send command to server (PORT 21)
int send_command(int sockfd, const char *cmd) {
    printf(">> Client request: %s", cmd);
    if (write(sockfd, cmd, strlen(cmd)) == -1) {
        perror("write");
        return -1;
    }

    return 0;
}

// receive command response from server
int receive_response(int sockfd, char *buffer, int size) {
    int n = read(sockfd, buffer, size - 1);
    if (n < 0) {
        perror("read");
        return -1;
    }

    buffer[n] = '\0';
    printf("<< Server response: %s", buffer);
    return n;
}

// parse PASV response from server => ip, port
int parse_pasv_response(const char *response, char *ip, int *port) {
    int h1, h2, h3, h4, p1, p2;
    int ret = sscanf(response, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2);

    if (ret != 6) {
        return -1;
    }

    sprintf(ip, "%d.%d.%d.%d", h1, h2, h3, h4);
    *port = p1 * 256 + p2;

    return 0;
}

// open data connection
int open_data_connection(const char *ip, int port) {
    int data_sock = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in data_addr;
    data_addr.sin_family = AF_INET;
    data_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &data_addr.sin_addr) < 0) {
        perror("connect data socket");
        return -1;
    }

    if (connect(data_sock, (struct sockaddr *) &data_addr, sizeof(data_addr)) < 0) {
        perror("connect data socket");
        return -1;
    }

    return data_sock;
}

// receive data from server (PORT 20)
void receive_data(int data_sock) {
    char buffer[4096];
    int n;

    while((n = read(data_sock,  buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';
        printf("%s", buffer);
    }

    close(data_sock);
}

int main() {
    char buffer[BUFFER_SIZE];
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    // create control socket
    int control_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (control_sock == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // set up server's address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(CONTROL_PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) == -1) {
        perror("inet_pton");
        close(control_sock);
        exit(EXIT_FAILURE);
    }

    // connect to server (control connection)
    if (connect(control_sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(control_sock);
        exit(EXIT_FAILURE);
    }

    // receive response from server
    receive_response(control_sock, buffer, sizeof(buffer));

    // Login
    send_command(control_sock, "USER user\r\n");
    receive_response(control_sock, buffer, sizeof(buffer));

    send_command(control_sock, "PASS pass\r\n");
    receive_response(control_sock, buffer, sizeof(buffer));

    // send PASV to open data connection
    send_command(control_sock, "PASV\r\n");

    // receive PASV response
    int total_n = 0;
    while(1) {
        int n = read(control_sock, buffer + total_n, sizeof(buffer) - total_n - 1);
        if (n <= 0) {
            break;
        }

        total_n += n;
        buffer[total_n] = '\0';
        if (strstr(buffer, "\r\n")) break;
    }

    if (total_n > 0) {
        printf("<< Server PASV response: %s", buffer);
    } else {
        printf("Failed to received PASV response\n");
        close(control_sock);
        exit(EXIT_FAILURE);
    }

    // parse PASV response
    char ip[IP_SIZE];
    int port;
    parse_pasv_response(buffer, ip, &port);

    // open data connection
    int data_sock = open_data_connection(ip, port);

    // send LIST 
    send_command(control_sock, "LIST\r\n");

    // receive response
    receive_response(control_sock, buffer, sizeof(buffer));

    // receive data
    receive_data(data_sock);

    // receive response from control socket
    receive_response(control_sock, buffer, sizeof(buffer) - 1);

    return 0;
}