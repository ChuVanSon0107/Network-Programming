# include <stdio.h>
# include <poll.h>
# include <arpa/inet.h>
# include <stdlib.h>
# include <unistd.h>
# include <string.h>

# define MAX_CLIENTS 100
# define PORT 8080
# define BUFFER_SIZE 1024
# define NAME_LEN 100

// send to everyone in this conversation
void broadcast_message(struct pollfd fds[], char buffer[], int sender_fd, char name[]) {
    char msg[BUFFER_SIZE + 50];
    snprintf(msg, sizeof(msg), "%s: %s", name, buffer);    

    for (int i = 1; i < MAX_CLIENTS + 1; i++) {
        if (fds[i].fd != -1 && fds[i].fd != sender_fd) {
            if (send(fds[i].fd, msg, strlen(msg), 0) == -1) {
                perror("send");
            }
        }
    }
}

// someone join the conversation
void broadcast_joining_message(struct pollfd fds[], int sender_fd, char name[]) {
    char msg[100];
    snprintf(msg, sizeof(msg), "%s join the conversation", name);

    for (int i = 1; i <= MAX_CLIENTS; i++) {
        if (fds[i].fd != -1 && fds[i].fd != sender_fd) {
            send(fds[i].fd, msg, strlen(msg), 0);
        }
    }
}

// someone left the conversation
void broadcast_lefting_message(struct pollfd fds[], int sender_fd, char name[]) {
    char msg[100];
    snprintf(msg, sizeof(msg), "%s left the conversation", name);

    for (int i = 1; i <= MAX_CLIENTS; i++) {
        if (fds[i].fd != -1 && fds[i].fd != sender_fd) {
            send(fds[i].fd, msg, strlen(msg), 0);
        }
    }
}

int main() {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int listenfd, clientfd;
    struct pollfd fds[MAX_CLIENTS + 1];
    char names[MAX_CLIENTS + 1][NAME_LEN];
    int nfds = 1; // so socket trong pollfd
    char buffer[BUFFER_SIZE];

    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));

    // Socket
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Bind
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(listenfd, MAX_CLIENTS) == -1) {
        perror("Listen failed");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    // Initialize pollfd
    for (int i = 0; i <= MAX_CLIENTS; i++) {
        fds[i].fd = -1;
        fds[i].events = 0;
        fds[i].revents = 0;
    }

    fds[0].fd = listenfd;
    fds[0].events = POLLIN;

    printf("Server is listening on port %d...\n", PORT);
    while(1) {
        // Count the sockets
        nfds = 0;
        for (int i = 0; i < MAX_CLIENTS + 1; i++) {
            if (fds[i].fd != -1) {
                nfds ++;
            }
        }

        // poll to see events
        int ret = poll(fds, MAX_CLIENTS + 1, -1);
        if (ret < 0) {
            perror("poll");
            break;
        }

        // listen socket
        if (fds[0].revents & POLLIN) {
            clientfd = accept(listenfd, (struct sockaddr *) &client_addr, &client_len);
            if (clientfd == -1) {
                perror("accept");
            } else {
                // add a client to pollfd
                if (nfds <  MAX_CLIENTS + 1) {
                    // name of client
                    int n = recv(clientfd, buffer, sizeof(buffer), 0);
                    if (n <= 0) {
                        break;
                    } else {
                        buffer[n] = '\0';
                    }

                    // send message to everyone
                    printf("%s has joined the conversation\n", buffer);
                    broadcast_joining_message(fds, clientfd, buffer);

                    for (int i = 1; i < MAX_CLIENTS + 1; i++) {
                        if (fds[i].fd == -1) {
                            fds[i].fd = clientfd;
                            fds[i].events = POLLIN;
                            nfds ++;

                            // store name
                            strcpy(names[i], buffer);
                            break;
                        }
                    }
                } else {
                    printf("The number of clients is too many, deny this connection\n");
                    close(clientfd);
                }
            }
        }

        // client socket
        for (int i = 1; i < MAX_CLIENTS + 1; i++) {
            if (fds[i].fd == -1) {
                continue;
            }

            // read from client
            if (fds[i].revents & POLLIN) {
                int n = recv(fds[i].fd,  buffer, sizeof(buffer) - 1, 0);

                if (n <= 0) {
                    // send lefting message
                    printf("%s left conversation\n", names[i]);
                    broadcast_lefting_message(fds, fds[i].fd, names[i]);

                    close(fds[i].fd);
                    fds[i].fd = -1;
                    continue;
                } else {
                    buffer[n] = '\0';
                    printf("Received from %s: %s\n", names[i], buffer);

                    // send to everyone in this conversation
                    broadcast_message(fds, buffer, fds[i].fd, names[i]);
                }
            }

            if (fds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                // send lefting message
                printf("%s left conversation\n", names[i]);
                broadcast_lefting_message(fds, fds[i].fd, names[i]);

                close(fds[i].fd);
                fds[i].fd = -1;
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS + 1; i++) {
        if (fds[i].fd != -1) {
            close(fds[i].fd);
        }
    }

    return 0;
}