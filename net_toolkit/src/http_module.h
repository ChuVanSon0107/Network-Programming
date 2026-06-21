#ifndef HTTP_MODULE_H
#define HTTP_MODULE_H

#include <stddef.h>

/* Open a TCP connection to the HTTP server. */
int http_connect_to_server(const char *server_ip, int server_port);

/* Send the entire HTTP request, including partial send handling. */
int http_send_all(int sockfd, const char *buffer, size_t buffer_length);

/* Receive the HTTP response until the server closes the connection. */
int http_receive_response(int sockfd, char *response,
                          size_t response_size);

/* Check status line and expected text in the HTTP response body. */
int check_http_response(const char *response, const char *expected_body);

/* Send GET request. Return 0 on success, -1 on error. */
int http_get_status(const char *server_ip, int server_port,
                    const char *host_header, const char *path);

#endif
