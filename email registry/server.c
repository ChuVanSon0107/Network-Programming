#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>

#define PORT 8080
#define IP "127.0.0.1"
#define POLL_MAXSIZE 20
#define BUFFER_SIZE 1024
#define EMAIL_SIZE 100
#define PASSWORD_SIZE 100

int connect_client(int listenfd);

int handle_client(int connfd, struct pollfd fds[], int i);

/* return: 1 if correct format, 0 if error */
int check_message_format(char buffer[]);

void retrieve_email_and_password(char buffer[], char email[], char password[]);

/* return: 1 if correct format, 0 if error */
int check_email(char email[]);

/* return: 1 if correct format, 0 if error */
int check_password(char password[]);

/* return: 1 if existed. 0 if error */
int check_existing_email(FILE *file, char email[]);

int main() {
    struct sockaddr_in server_addr;
    struct pollfd fds[POLL_MAXSIZE];
    int listenfd, connfd;

    /* Create TCP socket */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* Bind socket to specific IP and port */
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, IP, &server_addr.sin_addr) < 0) {
        perror("inet_pton");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    if (bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    /* Listen for incoming connections */
    if (listen(listenfd, 10) < 0) {
        perror("listen");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    printf("SERVER IS LISTENING ON PORT %d\n", PORT);

    /* Set up pollfd */
    for (int i = 0; i < POLL_MAXSIZE; i++) {
        fds[i].fd = -1;
    }

    fds[0].fd = listenfd;
    fds[0].events = POLLIN;

    while(1) {
        int ret = poll(fds, POLL_MAXSIZE, -1);
        if (ret == -1) {
            perror("poll");
            close(listenfd);
            exit(EXIT_FAILURE);
        } else if (ret == 0) {
            printf("Timeout 5s\n");
            break;
        } else {
            /* Listen socket */
            if (fds[0].revents & POLLIN) {
                /* Connect to client */
                connfd = connect_client(listenfd);

                if (connfd < 0) {
                    close(listenfd);
                    exit(EXIT_FAILURE);
                }

                /* Update pollfd */
                int ok = 0;
                for (int i = 0; i < POLL_MAXSIZE; i++) {
                    if (fds[i].fd == -1) {
                        fds[i].fd = connfd;
                        fds[i].events = POLLIN;
                        ok = 1;
                        break;
                    }
                }

                if (ok == 0) {
                    printf("A client cannot connect to Server because Server is full\n");
                } else {
                    printf("A client connected to Server\n");
                }
            }

            for (int i = 1; i < POLL_MAXSIZE; i++) {
                if (fds[i].revents & POLLIN) {
                    if (handle_client(fds[i].fd, fds, i) < 0) {
                        close(listenfd);
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }
    }

    close(listenfd);

    return 0;
}

int connect_client(int listenfd) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    int connfd = accept(listenfd, (struct sockaddr *)&client_addr, &addr_len);
    if (connfd < 0) {
        perror("accept");
        return -1;
    }

    return connfd;
}

int handle_client(int connfd, struct pollfd fds[], int i) {
    char buffer[BUFFER_SIZE], email[EMAIL_SIZE], password[PASSWORD_SIZE], response[1024];
    FILE *file = NULL;

    /* Receive email and password from client */
    int n = recv(connfd, buffer, sizeof(buffer) - 1, 0);
    
    /* Error in recv */
    if (n < 0) {
        perror("recv");
        close(connfd);
        return -1;
    } 

    /* Client close connection */
    if (n == 0) {
        /* Update pollfd */
        fds[i].fd = -1;
        close(connfd);
        printf("A client disconnected\n");
        return 0;
    } 

    /* Check email,password format */
    buffer[n] = '\0';

    /* check message format */
    if (check_message_format(buffer) == 0) {
        snprintf(response, sizeof(response), "ERROR Invalid message format\r\n");
        goto send_response;
    }

    /* retrieve email and password from message */
    retrieve_email_and_password(buffer, email, password);

    /* check email */
    if (check_email(email) == 0 || check_password(password) == 0) {
        snprintf(response, sizeof(response), "ERROR Invalid message format\r\n");
        goto send_response;
    }

    /* open users.txt file */
    file = fopen("users.txt", "r");
    if (file == NULL) {
        perror("fopen");
        close(connfd);
        return -1;
    }

    /* Check whether email is existed or not */
    if (check_existing_email(file, email)) {
        snprintf(response, sizeof(response), "ERROR Email has already existed\r\n");
        goto send_response;
    }

    fclose(file);
    file = NULL;

    /* Save email and password */
    file = fopen("users.txt", "a");
    if (file == NULL) {
        perror("fopen");
        close(connfd);
        return -1;
    }

    fprintf(file, "%s:%s\n", email, password);
    fclose(file);
    file = NULL;

    snprintf(response, sizeof(response), "OK Registry successfully\r\n");

send_response:
    if (send(connfd, response, strlen(response), 0) < 0) {
        perror("send");
        close(connfd);
        return -1;
    }

    if (file) {
        fclose(file);
    }

    fds[i].fd = -1;
    close(connfd);
    return 1;
}

int check_message_format(char buffer[]) {
    if (strchr(buffer, ',')) {
        return 1;
    }

    return 0;
}

void retrieve_email_and_password(char buffer[], char email[], char password[]) {
    char *comma = strchr(buffer, ',');
    *comma = '\0';

    strncpy(email, buffer, strlen(buffer));
    email[strlen(buffer)] = '\0';

    strncpy(password, comma + 1, strlen(comma + 1));
    password[strlen(comma + 1)] = '\0';
}

int check_email(char email[]) {
    char *suffix = strstr(email, "@example.com");
    if (suffix == NULL) {
        return 0;
    }

    if (strlen(suffix) != strlen("@example.com")) {
        return 0;
    }

    return 1;
}

int check_password(char password[]) {
    if (strchr(password, ',')) {
        return 0;
    }

    return 1;   
}

int check_existing_email(FILE *file, char email[]) {
    char buffer[BUFFER_SIZE];

    while(fgets(buffer, sizeof(buffer), file) != NULL) {
        if (strstr(buffer, email) == buffer) {
            return 1;
        }
    }

    return 0;
}

