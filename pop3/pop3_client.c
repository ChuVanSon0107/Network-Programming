#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define CMD_SIZE 1024
#define LINE_SIZE 4096
#define BUFFER_SIZE 4096

int connect_to_server(const char *server_ip, int server_port) {
    struct sockaddr_in server_addr;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

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

int recv_line(int sockfd, char *buffer, size_t buffer_size) {
    size_t i = 0;
    char ch;
    ssize_t n;

    while (i < buffer_size - 1) {
        n = recv(sockfd, &ch, 1, 0);
        if (n < 0) {
            perror("recv");
            return -1;
        } else if (n == 0) {
            /* Server closed connection */
            if (i == 0) return -1;
            break;
        }

        buffer[i] = ch;
        i ++;
        if (ch == '\n') break;
    }

    buffer[i] = '\0';

    return (int)i;
}

int check_pop3_ok(const char *response) {
    return strncmp(response, "+OK", 3) == 0;
}

int send_pop3_command(int sockfd, const char *command, char *response, size_t response_size) {
    printf("[CLIENT] %s\n", command);

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

int read_multiline_response(int sockfd) {
    char line[LINE_SIZE];

    while(1) {
        if (recv_line(sockfd, line, sizeof(line)) < 0) {
            fprintf(stderr, "[ERROR] Can't read multi-line response\n");
            return -1;
        }

        printf("%s", line);

        if (strcmp(line, ".\r\n") == 0 ||
            strcmp(line, ".\n")   == 0 ||
            strcmp(line, ".")     == 0) {
            break;
        }
    }

    return 0;
}

void run_pop3_menu(int sockfd) {
    char response[BUFFER_SIZE];
    char command[CMD_SIZE];
    int choice;
    int msg_num;

    while(1) {
        printf("===== POP3 Client Menu =====\n");
        printf("1. Show mailbox status (STAT)\n");
        printf("2. List messages       (LIST)\n");
        printf("3. Retrieve a message  (RETR)\n");
        printf("4. Quit                (QUIT)\n");
        printf("Choose an option: ");

        if (scanf("%d", &choice) != 1) {
            fprintf(stderr, "[ERROR] Your choice is invalid. Please choose number 1-4: \n\n");
            /* Xóa input buffer */
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            continue;
        }

        int c;
        while ((c = getchar()) != '\n' && c != EOF);
 
        printf("\n");

        switch (choice) {
            case 1: 
                snprintf(command, sizeof(command), "STAT\r\n");
                send_pop3_command(sockfd, command, response, sizeof(response));
                printf("\n");
                break;

            case 2:
                snprintf(command, sizeof(command), "LIST\r\n");
                printf("[CLIENT] LIST\n");
                if (send_all(sockfd, command) < 0) return;

                /* Nhận dòng đầu (+OK ...) */
                if (recv_line(sockfd, response, sizeof(response)) < 0) return;
                printf("[SERVER] %s", response);

                if (!check_pop3_ok(response)) {
                    fprintf(stderr, "[ERROR] LIST failed\n\n");
                    break;
                }

                /* Đọc phần nhiều dòng (danh sách email) */
                printf("--- List of Emails ---\n");
                read_multiline_response(sockfd);
                printf("-----------------------\n\n");
                break;
            
            case 3:
                printf("Input the number of email you want to read: ");
                if (scanf("%d", &msg_num) != 1 || msg_num <= 0) {
                    fprintf(stderr, "[ERROR] Invalid number\n\n");
                    while ((c = getchar()) != '\n' && c != EOF);
                    break;
                }
                while ((c = getchar()) != '\n' && c != EOF);
    
                snprintf(command, sizeof(command), "RETR %d\r\n", msg_num);
                printf("[CLIENT] RETR %d\n", msg_num);
                if (send_all(sockfd, command) < 0) return;
    
                /* Nhận dòng đầu (+OK ...) */
                if (recv_line(sockfd, response, sizeof(response)) < 0) return;
                printf("[SERVER] %s", response);
    
                if (!check_pop3_ok(response)) {
                    fprintf(stderr, "[ERROR] RETR %d failed.\n\n", msg_num);
                    break;
                }
    
                /* Đọc nội dung email nhiều dòng */
                printf("--- Content of Email #%d ---\n", msg_num);
                read_multiline_response(sockfd);
                printf("-------------------------\n\n");
                break;
    
            case 4:
                snprintf(command, sizeof(command), "QUIT\r\n");
                send_pop3_command(sockfd, command, response, sizeof(response));
                printf("\n[INFO] QUITED. Terminated POP3 session\n");
                return;   /* Thoát khỏi hàm, trở về main để đóng socket */
    
            default:
                fprintf(stderr, "[ERROR] Your choice is invalid. PLease input 1-4\n\n");
                break;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Using command: %s <server_ip> <server_port> <username> <password>\n"
            "Example: %s 127.0.0.1 1110 alice password\n", argv[0], argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip  = argv[1];
    int server_port = atoi(argv[2]);
    const char *username   = argv[3];
    const char *password   = argv[4];

    if (server_port <= 0 || server_port > 65535) {
        fprintf(stderr, "[ERROR] Invalid port: %s\n", argv[2]);
        exit(EXIT_FAILURE);
    }

    /* 1. Kết nối TCP */
    int sockfd = connect_to_server(server_ip, server_port);

    /* 2. Nhận greeting từ server */
    char response[BUFFER_SIZE];
    printf("[SERVER] Waiting greeting...\n");
    if (recv_line(sockfd, response, sizeof(response)) < 0) {
        fprintf(stderr, "Error: Not received greeting from server.\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("[SERVER] %s", response);
 
    if (!check_pop3_ok(response)) {
        fprintf(stderr, "Error: Server is not already (Not received +OK).\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* 3. Gửi USER */
    char command[CMD_SIZE];
    snprintf(command, sizeof(command), "USER %s\r\n", username);
    if (send_pop3_command(sockfd, command, response, sizeof(response)) < 0) {
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* 4. Gửi PASS */
    snprintf(command, sizeof(command), "PASS %s\r\n", password);
    if (send_pop3_command(sockfd, command, response, sizeof(response)) < 0) {
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("\n[INFO] Logged in with account: %s\n\n", username);

    /* 5. Hiển thị menu */
    run_pop3_menu(sockfd);
 
    close(sockfd);
    printf("[INFO] Closed connection.\n");
    return 0;
}