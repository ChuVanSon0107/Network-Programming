# include <stdio.h>
# include <arpa/inet.h>
# include <unistd.h>
# include <stdlib.h>
# include <string.h>
# include <sys/select.h>

# define PORT 8080
# define BUFFER_SIZE 1024

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

void prepare_response(char input[], char output[], char type) {
    output[0] = type;

    for (int i = 0; input[i] != '\0'; i++) {
        output[i + 1] = input[i];
    }

    output[strlen(input) + 1] = '\0';
}

void print_message(char str[]) {
    for (int i = 1; str[i] != '\0'; i++) {
        printf("%c", str[i]);
    }

    printf("\n");
}

int main() {
    int sockfd;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in server_addr, recv_addr;
    socklen_t addr_len = sizeof(recv_addr);
    char *server_ip = "127.0.0.1";
    fd_set read_fds;
    struct timeval timeout;

    // memset
    memset(&buffer, 0, sizeof(buffer));
    memset(&server_addr, 0, sizeof(server_addr));
    memset(&recv_addr, 0, sizeof(recv_addr));

    // Creating socket file descriptor
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Filling server information with specific IP address and port
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    // Convert the IP address from text to binary form
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) < 0) {
        perror("Invalid address or address not supported");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Connect to the server (associate the socket with the server)
    if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("Connect failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Send a message to server to start
    buffer[0] = START;
    buffer[1] = '\0';

    // encrypt and send to server
    xor_cipher(buffer, KEY);
    sendto(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *) &server_addr, addr_len);

    while(1) {
        // reset
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);

        // timeout
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;

        // select
        int result = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);

        if (result < 0) {
            perror("select failed");
            close(sockfd);
            exit(EXIT_FAILURE);
        } else if (result == 0) {
            printf("Timeout: Server didn't respond in 10 seconds.\n");
            close(sockfd);
            exit(EXIT_FAILURE);
        } else {
            if (FD_ISSET(sockfd, &read_fds)) {
                int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *) &recv_addr, &addr_len);
                buffer[n] = '\0';

                // Check server address
                if (!(memcmp(&server_addr.sin_port, &recv_addr.sin_port, sizeof(server_addr.sin_port)) == 0 &&
                    memcmp(&server_addr.sin_addr, &recv_addr.sin_addr, sizeof(server_addr.sin_addr)) == 0 && 
                    memcmp(&server_addr.sin_family, &recv_addr.sin_family, sizeof(server_addr.sin_family)) == 0)) {
                        printf("A message is received from wrong server!\n");
                        break;
                }

                // decrypt
                xor_cipher(buffer, KEY);

                // Message from server
                if (buffer[0] == MESSAGE) {
                    // print message
                    print_message(buffer);

                    // input from user
                    printf("Your answer: ");
                    char input[BUFFER_SIZE];
                    fgets(input, BUFFER_SIZE, stdin);
                    input[strlen(input) - 1] = '\0';

                    // prepare message to send to server
                    prepare_response(input, buffer, MESSAGE);

                    // encrypt 
                    xor_cipher(buffer, KEY);

                    // send to server
                    sendto(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *) &server_addr, addr_len);
                } 

                // ACK from server      
                if (buffer[0] == ACK) {
                    printf("ACK: Server received your message!\n");
                    break;
                }
            }
        }
    }

    // clean up
    close(sockfd);
    
    return 0;
}