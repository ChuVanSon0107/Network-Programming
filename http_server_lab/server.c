#include <stdio.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define HEADER_LENGTH 1024
#define METHOD_LENGTH 16
#define URI_LENGTH 256
#define VERSION_LENGTH 32
#define PATH_LENGTH 512

const char *get_content_type(char *path) {
    if (strstr(path, ".html")) return "text/html";
    if (strstr(path, ".css")) return "text/css";
    return "text/plain";
}

void sigchld_handler(int signo) {
    // WNOHANG: Not block 
    while(waitpid(-1, NULL, WNOHANG) > 0) {}
}

int read_and_send_response(char *path, FILE *file, char *buffer, int connectfd, const char *format) {
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

int send_not_found_response(char *buffer, int connectfd) {
    /* 404 Not Found */
    char path[] = "./not-found.html";

    /* open file */
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        printf("Server error: Error in handing file!\n");
        return -1;
    }

    /* send response */
    if (read_and_send_response(path, f, buffer, connectfd, "HTTP/1.1 404 Not Found\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n") == -1) {
        printf("Server error: Error in sending response\n");
        return -1;
    }

    /* close file */
    fclose(f);
    return 0;
}

int handle_method_not_allowed_request(char *buffer, int connectfd) {
    // 405 Method Not Allowed
    char path[] = "./method-not-allowed.html";

    /* open file */
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        printf("Server error: Error in handing file!\n");
        return -1;
    }

    /* send response */
    if (read_and_send_response(path, file, buffer, connectfd, "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n") == -1) {
        printf("Server error: Error in sending response\n");
        return -1;
    }

    /* close file */
    fclose(file);
    return 0;
}

int handle_get_request(char *path, char *buffer, int connectfd) {
    /* check whether the file exits */
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return send_not_found_response(buffer, connectfd);
    } else {
        /* send response */
        if (read_and_send_response(path, file, buffer, connectfd, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n") == -1) {
            printf("Server error: Error in sending response\n");
            return -1;
        }

        /* close file */
        fclose(file);
    }

    return 0;
}

int handle_post_request(char *path, char *buffer, int connectfd) {
    char *body = strstr(buffer, "\r\n\r\n");
    if (body == NULL) {
        printf("Request is formated wrongly!\n");
        return -1;
    }
    
    body += 4;
    printf("Data is written into data.txt file: %s\n", body);
    
    /* write data to a file */
    FILE *f = fopen("data.txt", "w");
    if (f == NULL) {
        printf("Server error: Error in handling file\n");
        return -1;
    }

    fprintf(f, "%s\n", body);

    /* Return message to server */
    strcpy(path, "./post.html");
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        printf("Server error: Error in handling file!\n");
        return -1;
    }

    /* send response */
    if (read_and_send_response(path, file, buffer, connectfd, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n") == -1) {
        printf("Server error: Error in sending response\n");
        return -1;
    }

    /* close file */
    fclose(file);

    return 0;
}

int handle_put_request(char *path, char *buffer, int connectfd) {
    char *body = strstr(buffer, "\r\n\r\n");
    if (body == NULL) {
        printf("Request is formated wrongly!\n");
        return -1;
    }
    body += 4;

    /* write data to a file */
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        printf("Server error: Error in handling file\n");
        return -1;
    }
    
    fprintf(f, "%s\n", body);
    printf("Data is updated in data.txt file: %s\n", body);

    /* Return message to server */
    strcpy(path, "./put.html");
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        printf("Server error: Error in handling file!\n");
        return -1;
    }

    /* send response */
    if (read_and_send_response(path, file, buffer, connectfd, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n") == -1) {
        printf("Server error: Error in sending response\n");
        return -1;
    }

    /* close file */
    fclose(file);

    return 0;
}

int handle_head_request(char *path, char *buffer, int connectfd) {
    /* open file */
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        char header[HEADER_LENGTH];
        int header_len = snprintf(header, HEADER_LENGTH, "HTTP/1.1 404 Not Found\r\nContent-Type: %s\r\nContent-Length: 0\r\n\r\n", get_content_type(path));
        if (send(connectfd, header, header_len, 0) == -1) {
            perror("send");
            return -1;
        }
    } else {
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
        int header_len = snprintf(header, HEADER_LENGTH, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n", get_content_type(path), filesize);
        if (send(connectfd, header, header_len, 0) == -1) {
            perror("send");
            return -1;
        }
    }

    return 0;
}

int handle_delete_request(char *path, char *buffer, int connectfd) {
    /* print the name of document which is deleted */
    printf("File need to be deleted: %s\n", path);

    /* check whether the file exits */
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return send_not_found_response(buffer, connectfd);
    } else {
        strcpy(path, "./delete.html");
        FILE *file = fopen(path, "rb");

        /* send response */
        if (read_and_send_response(path, file, buffer, connectfd, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n") == -1) {
            printf("Server error: Error in sending response\n");
            return -1;
        }

        /* close file */
        fclose(file);
    }

    return 0;
}
 
void handle_request(char *buffer, int connectfd) {
    char method[METHOD_LENGTH], uri[URI_LENGTH], version[VERSION_LENGTH], path[PATH_LENGTH];

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

        /* path to resource */
        if (strcmp(uri, "/") == 0) {
            strcpy(path, "./index.html");
        } else {
            snprintf(path, sizeof(path), ".%s", uri);
        }

        /* handle request */
        if (strcmp(method, "GET") == 0) {
            handle_get_request(path, buffer, connectfd);
        } else if (strcmp(method, "POST") == 0) {
            handle_post_request(path, buffer, connectfd);
        } else if (strcmp(method, "PUT") == 0) {
            handle_put_request(path, buffer, connectfd);
        } else if (strcmp(method, "HEAD") == 0) {
            handle_head_request(path, buffer, connectfd);
        } else if (strcmp(method, "DELETE") == 0) {
            handle_delete_request(path, buffer, connectfd);
        } else {
            handle_method_not_allowed_request(buffer, connectfd);
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