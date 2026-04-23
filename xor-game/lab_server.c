# include <stdio.h>
# include <arpa/inet.h>
# include <unistd.h>
# include <stdlib.h>
# include <sys/select.h>
# include <string.h>

# define PORT 8080
# define BUFFER_SIZE 1024
# define MAX_CLIENTS 10

# define START 0x01
# define ACK 0x02
# define MESSAGE 0x03

const char KEY[] = "mysecretkey";

void xor_cipher(char *data, const char *key) {
    int key_len = strlen(key);
    for (int i = 0; i < BUFFER_SIZE; i++) {
        data[i] ^= key[i % key_len];
    }
}

void prepare_response(char input[], char buffer[], char type) {
    // prepare message to send to server
    buffer[0] = type;

    for (int i = 0; input[i] != '\0'; i++) {
        printf("%c", input[i]);
        buffer[i + 1] = input[i];
    }
    printf("\n");
    buffer[strlen(input) + 1] = '\0';
}

void print_message(char str[], struct sockaddr_in client_addr) {
    char ip[40];
    int port = ntohs(client_addr.sin_port);
    inet_ntop(AF_INET, &client_addr.sin_addr, ip, INET_ADDRSTRLEN);
    printf("Message received from client %s:%d: ", ip, port);

    for (int i = 1; str[i] != '\0'; i++) {
        printf("%c", str[i]);
    }

    printf("\n");
}

int main() {
    struct sockaddr_in server_addr, client_addr;
    int sockfd;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    fd_set read_fds;
    struct timeval timeout;
    char *server_ip = "127.0.0.1";

    char questions[][BUFFER_SIZE] = {
        "Quenstion: 1 + 1 = ? \n A.1\n B.2\n C.3\n D.4\n",
        "Quenstion: 5 * 3 = ? \n A.15\n B.5\n C.10\n D.20\n",
        "Quenstion: 10 - 7 = ? \n A.6\n B.2\n C.3\n D.5\n",
        "Quenstion: 6 / 2 = ? \n A.2\n B.3\n C.4\n D.5\n",
        "Quenstion: 9 + 4 = ? \n A.12\n B.13\n C.14\n D.15\n",
        "Quenstion: 8 * 2 = ? \n A.14\n B.15\n C.16\n D.18\n",
        "Quenstion: 15 - 5 = ? \n A.5\n B.10\n C.15\n D.20\n",
        "Quenstion: 12 / 4 = ? \n A.2\n B.3\n C.4\n D.6\n",
        "Quenstion: 7 + 6 = ? \n A.11\n B.12\n C.14\n D.13\n",
        "Quenstion: 3 * 4 = ? \n A.12\n B.10\n C.7\n D.14\n"
    };

    // memset
    memset(&buffer, 0, sizeof(buffer));
    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));

    // create socket file descriptor
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // set up server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    
    // Convert the IP address from text to binary form
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) < 0) {
        perror("Invalid address or address not supported");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Bind the socket with the server address
    if (bind(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    while(1) {
        // reset
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);

        // timeout
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;

        // select
        int result = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);

        if (result == -1) {
            perror("select failed");
            close(sockfd);
            exit(EXIT_FAILURE);
        } else if (result == 0) {
            printf("Timeout: Client didn't respond in 10 seconds.\n");
        } else {
            if (FD_ISSET(sockfd, &read_fds)) {
                // Receive from clients
                int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *) &client_addr, &addr_len);
                buffer[n] = '\0';

                // decrypt
                xor_cipher(buffer, KEY);

                // Message from client to connect
                if (buffer[0] == START) {
                    char ip[40];
                    int port = ntohs(client_addr.sin_port);
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip, INET_ADDRSTRLEN);
                    printf("Client %s:%d connected to server!\n", ip, port);

                    // select questions
                    int id = rand() % 10;
                    prepare_response(questions[id], buffer, MESSAGE);

                    // encrypt and send to client
                    xor_cipher(buffer, KEY);
                    sendto(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *) &client_addr, addr_len);
                } 

                // Message from client to answer
                if (buffer[0] == MESSAGE) {
                    // printf message
                    print_message(buffer, client_addr);

                    // response
                    buffer[0] = ACK;
                    buffer[1] = '\0';

                    // encrypt 
                    xor_cipher(buffer, KEY);

                    // send message to client
                    sendto(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *) &client_addr, addr_len);
                }
            }
        }
    }

}