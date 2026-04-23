#include <stdio.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define HEADER_LENGTH 1024
#define METHOD_LENGTH 16
#define URI_LENGTH 256
#define VERSION_LENGTH 32

const char *get_content_type(char *path) {
    if (strstr(path, ".html")) return "text/html";
    if (strstr(path, ".css")) return "text/css";
    return "text/plain";
}

void sigchld_handler(int signo) {
    int saved_errno = errno;

    // WNOHANG: Not block 
    while(waitpid(-1, NULL, WNOHANG) > 0) {}

    errno = saved_errno;
}

int send_response(char *path, FILE *file, char *buffer, int connectfd, const char *format) {
    /* denotes to the end of this file */
    if (fseek(file, 0, SEEK_END) != 0) {
        perror("fseek");
        return -1;
    }

    /* return the position of pointer => the size of the file */
    long filesize = ftell(file);
    if (filesize < 0) {
        perror("ftell");
        return -1;
    }
    
    /* rewind to the beginning of the file */
    rewind(file);

    /* send header to client */
    char header[HEADER_LENGTH];
    int header_len = snprintf(header, HEADER_LENGTH, format, get_content_type(path), filesize);
    if (send(connectfd, header, header_len, 0) == -1) {
        perror("send");
        return -1;
    }

    /* send body */
    size_t bytes_read;
    while((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        if (send(connectfd, buffer, bytes_read, 0) == -1) {
            perror("send");
            return -1;
        }
    }

    return 0;
}

void handle_request(char *buffer, int connectfd) {
    char method[METHOD_LENGTH], uri[URI_LENGTH], version[VERSION_LENGTH], path[512];

    while(1) {
        /* receive HTTP request from client */
        int bytes = recv(connectfd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            printf("A client disconnected from server!\n");
            return;
        } else if (bytes >= BUFFER_SIZE) {
            printf("The length of request from client is too large to process!\n");
            return;
        }
        
        buffer[bytes] = '\0';

        /* parse HTTP request */
        sscanf(buffer, "%15s %255s %31s", method, uri, version);

        if (strcmp(method, "GET") == 0) {
            /* path to resource */
            if (strcmp(uri, "/") == 0) {
                strcpy(path, "./index.html");
            } else {
                snprintf(path, sizeof(path), ".%s", uri);
            }

            /* check whether the file exits */
            FILE *file = fopen(path, "rb");
            if (file == NULL) {
                /* 404 Not Found */
                strcpy(path, "./not-found.html");

                /* open file */
                FILE *f = fopen(path, "rb");
                if (f == NULL) {
                    printf("Server error: Error in handing file!\n");
                    return;
                }

                /* send response */
                if (send_response(path, f, buffer, connectfd, "HTTP/1.1 404 Not Found\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n") == -1) {
                    printf("Server error: Error in sending response\n");
                }

                /* close file */
                fclose(f);

            } else {
                /* send response */
                if (send_response(path, file, buffer, connectfd, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n") == -1) {
                    printf("Server error: Error in sending response\n");
                }

                /* close file */
                fclose(file);
            }

        } else {
            // 405 Method Not Allowed
            strcpy(path, "./method-not-allowed.html");

            /* open file */
            FILE *file = fopen(path, "rb");
            if (file == NULL) {
                printf("Server error: Error in handing file!\n");
                return;
            }

            /* send response */
            if (send_response(path, file, buffer, connectfd, "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n") == -1) {
                printf("Server error: Error in sending response\n");
            }

            /* close file */
            fclose(file);
        }
    }
}

int main() {
    int listenfd, connectfd;;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];

    // signal to collect zombie process
    signal(SIGCHLD, sigchld_handler);

    // create listen socket
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // set up server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // bind socket to specific port
    if (bind(listenfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    // listen for incoming connections
    if (listen(listenfd, 10) == -1) {
        perror("listen");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on port %d...\n", PORT);

    while(1) {
        // accept connection from clients
        connectfd = accept(listenfd, (struct sockaddr *) &client_addr, &addr_len);
        if (connectfd == -1) {
            perror("accept");
            continue;
        }

        printf("A client connected to server!\n");

        // fork
        pid_t pid = fork();
        
        if (pid < 0) {
            perror("fork failed");
        } else if (pid > 0) {
            close(connectfd);
        } else {
            // close listen socket in child process
            close(listenfd);

            // handle HTTP request from server
            handle_request(buffer, connectfd);

            close(connectfd);
            exit(EXIT_SUCCESS);
        }
    }

    // close socket
    close(listenfd);

    return 0;
}