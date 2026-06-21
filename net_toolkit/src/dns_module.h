#ifndef DNS_MODULE_H
#define DNS_MODULE_H

#include <stddef.h>
#include <stdint.h>

/* Helper functions based on dns/dns_client.c. */
int validate_ip(const char *ip);
int create_udp_socket(void);

int build_dns_query(unsigned char *query, size_t query_size,
                    const char *domain, uint16_t query_id);

void encode_domain_name(unsigned char *buffer, const char *domain);

int send_dns_query(int sockfd, const char *dns_server_ip, int dns_server_port,
                   const unsigned char *query, int query_len);

int receive_dns_response(int sockfd, unsigned char *response,
                         size_t response_size);

int parse_dns_response(const unsigned char *response, int response_len,
                       uint16_t expected_id, char *ip_buffer,
                       size_t ip_buffer_size);

int skip_dns_name(const unsigned char *message, int message_len, int pos);

/* Main function used by net_toolkit. Return 0 on success, -1 on error. */
int dns_resolve_a(const char *dns_server_ip, int dns_server_port,
                  const char *domain, char *ip_buffer,
                  size_t ip_buffer_size);

#endif
