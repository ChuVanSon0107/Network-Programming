#include "dns_module.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/* Constants */
#define QUERY_BUFFER 512
#define RESPONSE_BUFFER 512
#define QUERY_ID 0x1234
#define DNS_TIMEOUT_SECONDS 5

/* DNS record types */
#define TYPE_A 1

/* DNS classes */
#define CLASS_IN 1

/* --------------------
DNS Message = Header + QUESTION Section + ANSWER Section
              + AUTHORITY Section + ADDITIONAL Section
Header (12 bytes) = ID + Flags + QDCOUNT + ANCOUNT + NSCOUNT + ARCOUNT
QUESTION Section = QNAME + QTYPE + QCLASS
ANSWER Section = NAME + TYPE + CLASS + TTL + RDLENGTH + RDATA
----------------------- */

/* DNS header struct (12 bytes fixed) */
struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
};

int validate_ip(const char *ip) {
    struct in_addr addr;

    if (ip == NULL || inet_pton(AF_INET, ip, &addr) <= 0) {
        return 0;
    }

    return 1;
}

int create_udp_socket() {
    int sockfd;
    struct timeval timeout;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    timeout.tv_sec = DNS_TIMEOUT_SECONDS;
    timeout.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) < 0 ||
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO,
                   &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

void encode_domain_name(unsigned char *buffer, const char *domain) {
    char domain_copy[256];
    char *token;

    strcpy(domain_copy, domain);
    token = strtok(domain_copy, ".");

    while(token != NULL) {
        size_t len = strlen(token);
        *buffer++ = (unsigned char)len;
        memcpy(buffer, token, len);
        buffer += len;
        token = strtok(NULL, ".");
    }

    *buffer++ = 0;
}

int build_dns_query(unsigned char *query, size_t query_size,
                    const char *domain, uint16_t query_id) {
    struct dns_header *header;
    unsigned char *ptr;
    uint16_t qtype;
    uint16_t qclass;

    if (query == NULL || query_size < sizeof(struct dns_header) + 5) {
        return -1;
    }

    memset(query, 0, query_size);

    /* DNS Header */
    header = (struct dns_header *)query;
    header->id = htons(query_id);
    header->flags = htons(0x0100);       /* Recursion desired */
    header->qdcount = htons(1);
    header->ancount = 0;
    header->nscount = 0;
    header->arcount = 0;

    /* DNS Question Section: QNAME, QTYPE, QCLASS */
    ptr = query + sizeof(struct dns_header);
    encode_domain_name(ptr, domain);
    ptr += strlen(domain) + 2;

    /* QTYPE = A */
    qtype = htons(TYPE_A);
    memcpy(ptr, &qtype, sizeof(qtype));
    ptr += sizeof(qtype);

    /* QCLASS = IN */
    qclass = htons(CLASS_IN);
    memcpy(ptr, &qclass, sizeof(qclass));
    ptr += sizeof(qclass);

    return (int)(ptr - query);
}

int send_dns_query(int sockfd, const char *dns_server_ip, int dns_server_port,
                   const unsigned char *query, int query_len) {
    ssize_t sent;
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    /* Configure server address */
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)dns_server_port);
    if (inet_pton(AF_INET, dns_server_ip, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        return -1;
    }

    /* Send DNS query */
    sent = sendto(sockfd, query, query_len, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (sent < 0) {
        perror("sendto");
        return -1;
    }

    printf("[DNS] Query sent to %s:%d\n", dns_server_ip, dns_server_port);
    return 0;
}


int receive_dns_response(int sockfd, unsigned char *response, size_t response_size) {
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);
    ssize_t received;

    /* Receive DNS answer */
    received = recvfrom(sockfd, response, response_size, 0, (struct sockaddr *)&server_addr, &addr_len);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            fprintf(stderr, "[ERROR] DNS response timeout\n");
        } else {
            perror("recvfrom");
        }
        return -1;
    }

    printf("[DNS] Response received\n");
    return (int)received;
}

int skip_dns_name(const unsigned char *message, int messsage_len, int pos) {
    while (pos < messsage_len) {
        unsigned char len = message[pos];

        /* Compression pointer */
        if ((len & 0xC0) == 0xC0) {
            return pos + 2;
        }

        /* end of domain name */
        if (len == 0) {
            return pos + 1;
        }

        pos += len + 1;
    }

    return -1;
}

int parse_dns_response(const unsigned char *response, int response_len,
                       uint16_t expected_id, char *ip_buffer,
                       size_t ip_buffer_size) {
    const struct dns_header *header;
    uint16_t response_id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    int pos;

    if (response == NULL ||
        response_len < (int)sizeof(struct dns_header)) {
        fprintf(stderr, "[ERROR] DNS response is too short\n");
        return -1;
    }

    header = (const struct dns_header *)response;

    /* Verify Response ID. */
    response_id = ntohs(header->id);
    if (response_id != expected_id) {
        fprintf(stderr,
                "[ERROR] Response ID mismatch "
                "(expected 0x%04X, got 0x%04X)\n",
                expected_id, response_id);
        return -1;
    }

    /* Verify that this packet is a response and RCODE is zero. */
    flags = ntohs(header->flags);
    if ((flags & 0x8000) == 0 || (flags & 0x000F) != 0) {
        fprintf(stderr, "[ERROR] Invalid DNS response flags: 0x%04X\n",
                flags);
        return -1;
    }

    qdcount = ntohs(header->qdcount);
    ancount = ntohs(header->ancount);
    if (ancount == 0) {
        fprintf(stderr, "[ERROR] No DNS answer records found\n");
        return -1;
    }

    printf("[DNS] Answer records: %u\n", ancount);
    pos = (int)sizeof(struct dns_header);

    /* Skip Question Section. */
    for (uint16_t question = 0; question < qdcount; question++) {
        pos = skip_dns_name(response, response_len, pos);
        if (pos < 0 || pos + 4 > response_len) {
            fprintf(stderr, "[ERROR] Invalid DNS Question Section\n");
            return -1;
        }

        /* Skip QTYPE and QCLASS. */
        pos += 4;
    }

    /* Parse Answer Section and return the first IPv4 address. */
    for (uint16_t answer = 0; answer < ancount; answer++) {
        uint16_t type;
        uint16_t class_code;
        uint16_t rdlength;

        pos = skip_dns_name(response, response_len, pos);
        if (pos < 0 || pos + 10 > response_len) {
            fprintf(stderr, "[ERROR] Invalid DNS Answer Section\n");
            return -1;
        }

        /* Read TYPE (2 bytes) */
        memcpy(&type, response + pos, sizeof(type));
        type = ntohs(type);
        pos += 2;

        /* Read CLASS (2 bytes) */
        memcpy(&class_code, response + pos, sizeof(class_code));
        class_code = ntohs(class_code);
        pos += 2;

        /* Skip TTL (4 bytes). */
        pos += 4;

        /* Read RDLENGTH */
        memcpy(&rdlength, response + pos, sizeof(rdlength));
        rdlength = ntohs(rdlength);
        pos += 2;

        if (pos + rdlength > response_len) {
            fprintf(stderr, "[ERROR] DNS RDATA is truncated\n");
            return -1;
        }

        if (type == TYPE_A && class_code == CLASS_IN && rdlength == 4) {
            if (ip_buffer_size < INET_ADDRSTRLEN ||
                inet_ntop(AF_INET, response + pos,
                          ip_buffer, ip_buffer_size) == NULL) {
                fprintf(stderr, "[ERROR] Cannot convert IPv4 address\n");
                return -1;
            }

            printf("[DNS] IPv4 address: %s\n", ip_buffer);
            return 0;
        }

        pos += rdlength;
    }

    fprintf(stderr, "[ERROR] No IPv4 A record found\n");
    return -1;
}

int dns_resolve_a(const char *dns_server_ip, int dns_server_port,
                  const char *domain, char *ip_buffer,
                  size_t ip_buffer_size) {
    int sockfd = -1;
    unsigned char query[QUERY_BUFFER];
    unsigned char response[RESPONSE_BUFFER];
    int query_len;
    int response_len;
    int result = -1;

    if (!validate_ip(dns_server_ip)) {
        fprintf(stderr, "[ERROR] Invalid DNS server IP address\n");
        return -1;
    }

    if (dns_server_port <= 0 || dns_server_port > 65535) {
        fprintf(stderr, "[ERROR] Invalid DNS server port: %d\n",
                dns_server_port);
        return -1;
    }

    if (domain == NULL || ip_buffer == NULL ||
        ip_buffer_size < INET_ADDRSTRLEN) {
        fprintf(stderr, "[ERROR] Invalid DNS resolver argument\n");
        return -1;
    }

    /* 1. Create UDP socket. */
    sockfd = create_udp_socket();
    if (sockfd < 0) {
        return -1;
    }

    /* 2. Build DNS query packet. */
    query_len = build_dns_query(query, sizeof(query), domain, QUERY_ID);
    if (query_len < 0) {
        goto cleanup;
    }

    /* 3. Send DNS query to the configured server and port. */
    if (send_dns_query(sockfd, dns_server_ip, dns_server_port,
                       query, query_len) < 0) {
        goto cleanup;
    }

    /* 4. Receive DNS response. */
    response_len = receive_dns_response(sockfd, response, sizeof(response));
    if (response_len < 0) {
        goto cleanup;
    }

    /* 5. Parse response and copy the first A record to ip_buffer. */
    result = parse_dns_response(response, response_len, QUERY_ID,
                                ip_buffer, ip_buffer_size);

cleanup:
    close(sockfd);
    return result;
}
