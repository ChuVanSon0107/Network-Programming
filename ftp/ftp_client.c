#include <stdio.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define BUFFER_SIZE 4096
#define CONTROL_PORT 21
#define DATA_PORT 20
#define IP_SIZE 16
#define COMMAND_SIZE 100

// send command to server (PORT 21)
int send_command(int sockfd, const char *cmd) {
    printf(">> Client request: %s", cmd);
    if (write(sockfd, cmd, strlen(cmd)) == -1) {
        perror("write");
        return -1;
    }

    return 0;
}

// receive command response from server
int receive_response(int sockfd) {
    char buffer[BUFFER_SIZE];

    int n = read(sockfd, buffer, sizeof(buffer) - 1);
    if (n < 0) {
        perror("read");
        return -1;
    }

    buffer[n] = '\0';
    printf("<< Server response: %s", buffer);
    return n;
}

// open data connection
int open_data_connection(const char *ip, int port) {
    int data_sock = socket(AF_INET, SOCK_STREAM, 0);

    if (data_sock == -1) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in data_addr;
    data_addr.sin_family = AF_INET;
    data_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &data_addr.sin_addr) < 0) {
        perror("connect data socket");
        return -1;
    }

    if (connect(data_sock, (struct sockaddr *) &data_addr, sizeof(data_addr)) < 0) {
        perror("connect data socket");
        return -1;
    }

    return data_sock;
}

// PASSIVE MODE
void send_pasv(int control_sock, char *ip, int *port) {
    char buffer[BUFFER_SIZE];

    // send PASV
    send_command(control_sock, "PASV\r\n");

    // receive PASV response
    int total_n = 0;
    while(1) {
        int n = read(control_sock, buffer + total_n, sizeof(buffer) - total_n - 1);
        if (n <= 0) {
            break;
        }

        total_n += n;
        buffer[total_n] = '\0';
        if (strstr(buffer, "\r\n")) break;
    }

    if (total_n > 0) {
        printf("<< Server PASV response: %s", buffer);
    } else {
        printf("Failed to received PASV response\n");
        close(control_sock);
        exit(EXIT_FAILURE);
    }

    int h1, h2, h3, h4, p1, p2;
    int ret = sscanf(buffer, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2);

    if (ret != 6) {
        printf("Server's response is not formatted correctly\n");
        return;
    }

    sprintf(ip, "%d.%d.%d.%d", h1, h2, h3, h4);
    *port = p1 * 256 + p2;
}

int connect_to_server() {
    char buffer[BUFFER_SIZE];
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    // create control socket
    int control_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (control_sock == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // set up server's address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(CONTROL_PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) == -1) {
        perror("inet_pton");
        close(control_sock);
        exit(EXIT_FAILURE);
    }

    // connect to server (control connection)
    if (connect(control_sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(control_sock);
        exit(EXIT_FAILURE);
    }

    return control_sock;
}

// Login
void ftp_login(int control_sock) {
    send_command(control_sock, "USER user\r\n");
    receive_response(control_sock);

    send_command(control_sock, "PASS pass\r\n");
    receive_response(control_sock);
}

// LIST
void ftp_list(int control_sock) {
    char buffer[BUFFER_SIZE], ip[16];
    int port, n, data_sock;
    
    // send PASV 
    send_pasv(control_sock, ip, &port);

    // open data connection
    data_sock = open_data_connection(ip, port);

    // send LIST command
    send_command(control_sock, "LIST\r\n");

    // receive LIST response
    receive_response(control_sock);

    // receive LIST response
    while((n = read(data_sock,  buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';
        printf("%s", buffer);
    }

    // close data socket
    close(data_sock);

    // receive response from control socket
    receive_response(control_sock);
}

// RETR
void ftp_retr(int control_sock, const char *filename) {
    char buffer[BUFFER_SIZE], ip[IP_SIZE], command[COMMAND_SIZE];
    int port, n, data_sock;
    FILE *fp;

    // send PASV
    send_pasv(control_sock, ip, &port);

    // open data connection
    data_sock = open_data_connection(ip, port);

    // send RETR 
    sprintf(command, "RETR %s\r\n", filename);
    send_command(control_sock, command);

    // receive RETR response
    receive_response(control_sock);

    // receive data
    fp = fopen(filename, "wb");
    if (!fp) {
        perror("fopen");
        close(data_sock);
        return;
    }

    while((n = read(data_sock, buffer, sizeof(buffer) - 1)) > 0) {
        fwrite(buffer, 1, n, fp);
    }

    fclose(fp);
    
    // close data socket
    close(data_sock);
    
    // receive response from control socket
    receive_response(control_sock);
}

// STOR
void ftp_stor(int control_sock, const char *filename) {
    char buffer[BUFFER_SIZE], ip[IP_SIZE], command[COMMAND_SIZE];
    int port, n, data_sock;
    FILE *fp;

    // send PASV
    send_pasv(control_sock, ip, &port);

    // open data connection
    data_sock = open_data_connection(ip, port);

    // send STOR filename command
    snprintf(command, sizeof(command), "STOR %s\r\n", filename);
    send_command(control_sock, command);

    // receive RETR response
    receive_response(control_sock);

    // read local file
    fp = fopen(filename, "rb");
    if (!fp) {
        perror("fopen");
        close(data_sock);
        return;
    }

    // send file through data connection
    while((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        write(data_sock, buffer, n);
    }

    fclose(fp);

    // close data socket
    close(data_sock);

    // receive response from control socket
    receive_response(control_sock);
}

// DELE
void ftp_delete(int control_sock, const char *filename) {
    char command[COMMAND_SIZE], buffer[BUFFER_SIZE];

    // send DELETE filename to server
    snprintf(command, sizeof(command), "DELE %s\r\n", filename);
    send_command(control_sock, command);

    // receive response from server
    receive_response(control_sock);
}

// CWD
void ftp_cwd(int control_sock, const char *dirname) {
    char command[COMMAND_SIZE], buffer[BUFFER_SIZE];

    // send CWD dirname
    snprintf(command, sizeof(command), "CWD %s\r\n", dirname);
    send_command(control_sock, command);

    // receive response from server
    receive_response(control_sock);
}

// QUIT
void ftp_quit(int control_sock) {
    send_command(control_sock, "QUIT\r\n");
    receive_response(control_sock);
}

void print_menu() {
    printf("\n===== FTP CLIENT MENU =====\n");
    printf("1. List files (LIST)\n");
    printf("2. Download file (RETR)\n");
    printf("3. Upload file (STOR)\n");
    printf("4. Delete file (DELE)\n");
    printf("5. Change directory (CWD)\n");
    printf("6. Quit\n");
    printf("===========================\n");
}

int main() {
    char  choice[10], filename[256], dirname[256];
    int control_sock;
    control_sock = connect_to_server();

    // receive response from server
    receive_response(control_sock);

    // Login
    ftp_login(control_sock);

    while(1) {
        print_menu();
        printf("Enter your choice: ");
        fgets(choice, sizeof(choice), stdin);

        if (strncmp(choice, "1", 1) == 0) {
            ftp_list(control_sock);
        } else if (strncmp(choice, "2", 1) == 0) {
            printf("Enter filename to download: ");
            fgets(filename, sizeof(filename), stdin);
            filename[strcspn(filename, "\n")] = '\0';
            ftp_retr(control_sock, filename);
        } else if (strncmp(choice, "3", 1) == 0) {
            printf("Enter filename to upload: ");
            fgets(filename, sizeof(filename), stdin);
            filename[strcspn(filename, "\n")] = '\0';
            ftp_stor(control_sock, filename);
        } else if (strncmp(choice, "4", 1) == 0) {
            printf("Enter filename to delete: ");
            fgets(filename, sizeof(filename), stdin);
            filename[strcspn(filename, "\n")] = '\0';
            ftp_delete(control_sock, filename);
        } else if (strncmp(choice, "5", 1) == 0) {
            printf("Enter directory to switch to: ");
            fgets(dirname, sizeof(dirname), stdin);
            dirname[strcspn(dirname, "\n")] = '\0';
            ftp_cwd(control_sock, dirname);
        } else if (strncmp(choice, "6", 1) == 0) {
            ftp_quit(control_sock);
            close(control_sock);
            break;
        } else {
            printf("Invalid option.\n");
        }
    }

    return 0;
}