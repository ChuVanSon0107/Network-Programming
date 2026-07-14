#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>

#define PORT 8080
#define IP "127.0.0.1"
#define BUFFER_SIZE 1024

int main() {
    struct sockaddr_in server_addr;
    int sockfd;
    char buffer[BUFFER_SIZE];;

    /* Create socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* Set up server address */
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, IP, &server_addr.sin_addr) < 0) {
        perror("inet_pton");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Connect to server */
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Enter message */
    printf("Enter email, password: ");
    fgets(buffer, sizeof(buffer) - 1, stdin);
    buffer[strlen(buffer) - 1] = '\0';

    /* Send request to server */
    if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
        perror("send");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Receive response from server */
    int n = recv(sockfd, buffer, sizeof(buffer), 0);
    if (n < 0) {
        perror("recv");
        close(sockfd);
        exit(EXIT_FAILURE);
    } else if (n == 0) {
        close(sockfd);
        printf("Server closed connection\n");
        exit(EXIT_SUCCESS);
    } else {
        buffer[n] = '\0';
        printf("Response from server: %s", buffer);
        
        close(sockfd);
        exit(EXIT_SUCCESS);
    }

    return 0;
}