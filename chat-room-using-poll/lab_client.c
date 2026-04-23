# include <stdio.h>
# include <arpa/inet.h>
# include <stdlib.h>
# include <string.h>
# include <unistd.h>
# include <poll.h>

# define BUFFER_SIZE 1024
# define PORT 8080
# define IP "127.0.0.1"

int main() {
    struct sockaddr_in server_addr;
    int sockfd;
    char buffer[BUFFER_SIZE];
    struct pollfd fds[2];


    // create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // config server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    
    if (inet_pton(AF_INET, IP, &server_addr.sin_addr) == -1) {
        perror("inet_pton");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // connect to the server
    if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // input from user
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    // server
    fds[1].fd = sockfd;
    fds[1].events = POLLIN;

    // Name of user
    printf("Enter your name: ");
    fgets(buffer, sizeof(buffer), stdin);
    buffer[strlen(buffer) - 1] = '\0';

    // Send to server
    if (send(sockfd, buffer, strlen(buffer), 0) == -1) {
        perror("send");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    while(1) {
        int ret = poll(fds, 2, -1);
        if (ret == -1) {
            perror("poll");
            break;
        }

        // server  sends data
        if (fds[1].revents & POLLIN) {
            int n = recv(sockfd, buffer, sizeof(buffer), 0);
            if (n == -1) {
                perror("recv");
                break;
            } else if (n == 0) {
                printf("Server disconnected\n");
                break;
            } else {
                buffer[n] = '\0';
                printf("%s\n", buffer);
            }
        }

        if (fds[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            printf("Server closed connection or socket error\n");
            break;
        }     

        // input from client
        if (fds[0].revents & POLLIN) {
            fgets(buffer, sizeof(buffer), stdin);
            buffer[strlen(buffer) - 1] = '\0';

            // exit => thoat
            if (strncmp(buffer, "exit", 4) == 0) {
                printf("Disconnecting...\n");
                break;
            }

            if (send(sockfd, buffer, strlen(buffer), 0) == -1) {
                perror("send");
                break;
            }
        }
    }

    close(sockfd);
    return 0;
}