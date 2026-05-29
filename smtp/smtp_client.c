#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

#define BUFFER_SIZE 4096
#define CMD_SIZE 1024

/* Kết nối tới SMTP server, trả về socket fd hoặc thoát nếu lỗi */
int connect_to_server(const char *server_ip, int server_port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("[ERROR] socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("[ERROR] inet_pton");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[ERROR] connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("[INFO] Connected to %s:%d\n", server_ip, server_port);

    return sockfd;
}

/* Gửi toàn bộ buffer (đảm bảo gửi hết dù send() trả về ít hơn) */
int send_all(int sockfd, const char *buffer) {
    size_t total = strlen(buffer);
    size_t sent = 0;

    while(sent < total) {
        ssize_t n = send(sockfd, buffer + sent, total - sent, 0);

        if (n < 0) {
            perror("[ERROR] send");
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        sent += (size_t)n;
    }

    return 0;
}

/* Nhận phản hồi từ server, lưu vào buffer */
int recv_response (int sockfd , char *buffer , size_t buffer_size ) {
    memset(buffer, 0, buffer_size);
    ssize_t n = recv(sockfd, buffer, buffer_size - 1, 0);

    if (n < 0) {
        perror("[ERROR] recv");
        close(sockfd);
        exit(EXIT_FAILURE);
    } else if (n == 0) {
        fprintf(stderr, "[ERROR] Server closed connection suddenly\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    } else {
        buffer[n] = '\0';
        printf("[SERVER] %s", buffer);
    }

    return 0;
}

/* Kiểm tra mã phản hồi có khớp expected_code không */
int check_response_code(const char *response, const char *expected_code) {
    return strncmp(response, expected_code, 3) == 0;
}

/* Gửi lệnh SMTP, nhận phản hồi và kiểm tra mã */
int send_smtp_command(int sockfd, const char *command, const char *expected_code) {
    char buffer[BUFFER_SIZE];

    printf("[CLIENT] %s", command);
    send_all(sockfd, command);
    recv_response(sockfd, buffer, sizeof(buffer));

    if (!check_response_code(buffer, expected_code)) {
        fprintf(stderr, "[ERROR] Unexpected SMTP response: %s\n", buffer);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    return 0;
}

/* Đọc nội dung email từ bàn phím và gửi từng dòng tới server */
void read_email_body_and_send(int sockfd) {
    char line[CMD_SIZE];
    printf("[INFO] Input conent of email:\n");

    while(1) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        if (strcmp(line, ".\n") == 0 || strcmp(line, ".\r\n") == 0 || strcmp(line, ".") == 0) {
            break;
        }

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            if (len > 1 && line[len - 2] == '\r') {
                line[len - 2] = '\0';
            }
        }

        char smtp_line[CMD_SIZE + 4];
        snprintf(smtp_line, sizeof(smtp_line), "%s\r\n", line);
        send_all(sockfd, smtp_line);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 6) {
        fprintf(stderr, "Using command: %s <server_ip> <server_port> <sender> <recipient> <subject>\n"
            "Example: %s 127.0.0.1 1025 alice@example.com bob@example.com \"Hello\"\n", argv[0], argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);
    const char *sender = argv[3];
    const char *recipient = argv[4];
    const char *subject = argv[5];

    if (server_port <= 0 || server_port > 65535) {
        fprintf(stderr, "[ERROR] Invalid port: %s\n", argv[2]);
        exit(EXIT_FAILURE);
    }

    /* 1. Tạo socket và kết nối */
    int sockfd = connect_to_server(server_ip, server_port);

    /* 2. Nhận response 220 từ server */
    char buffer[BUFFER_SIZE];
    recv_response(sockfd, buffer, sizeof(buffer));
    if (!check_response_code(buffer, "220")) {
        fprintf(stderr, "[ERROR] Unexpected SMTP response\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* 3. HELO */
    char cmd[CMD_SIZE];
    snprintf(cmd, sizeof(cmd), "HELO localhost\r\n");
    send_smtp_command(sockfd, cmd, "250");

    /* 4. MAIL FROM */
    snprintf(cmd, sizeof(cmd), "MAIL FROM:<%s>\r\n", sender);
    send_smtp_command(sockfd, cmd, "250");

    /* 5. RCPT TO */
    snprintf(cmd, sizeof(cmd), "RCPT TO:<%s>\r\n", recipient);
    send_smtp_command(sockfd, cmd, "250");

    /* 6. DATA */
    snprintf(cmd, sizeof(cmd), "DATA\r\n");
    send_smtp_command(sockfd, cmd, "354");

    /* 7. Gửi header email */
    snprintf(cmd, sizeof(cmd), "From: %s\r\n", sender);
    send_all(sockfd, cmd);
 
    snprintf(cmd, sizeof(cmd), "To: %s\r\n", recipient);
    send_all(sockfd, cmd);
 
    snprintf(cmd, sizeof(cmd), "Subject: %s\r\n", subject);
    send_all(sockfd, cmd);

    /* Dòng trống phân tách header và body */
    send_all(sockfd, "\r\n");

    /* 8. Đọc và gửi body */
    read_email_body_and_send(sockfd);

    /* 9. Kết thúc DATA bằng \r\n.\r\n */
    send_all(sockfd, "\r\n.\r\n");
    recv_response(sockfd, buffer, sizeof(buffer));
    if (!check_response_code(buffer, "250")) {
        fprintf(stderr, "[ERROR] Server didn't accept the content of email: %s\n", buffer);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* 10. QUIT */
    snprintf(cmd, sizeof(cmd), "QUIT\r\n");
    printf("[CLIENT] %s", cmd);
    send_all(sockfd, cmd);
    recv_response(sockfd, buffer, sizeof(buffer));
 
    /* 11. Đóng socket */
    close(sockfd);
    printf("[INFO] Gửi email thành công! Kiểm tra tại http://localhost:8025\n");
 
    return 0;
}