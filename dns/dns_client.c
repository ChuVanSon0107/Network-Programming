#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

/* Constants */
#define DNS_PORT 53
#define QUERY_BUFFER 512
#define RESPONSE_BUFFER 512
#define QUERY_ID 0x1234

/* DNS record types */
#define TYPE_A 1

/* DNS classes */
#define CLASS_IN 1

/* -------------------- 
DNS Message = Header + QUESTION Section + ANSWER Section + AUTHORITY Section + ADDITIONAL Section 
Header (12 bytes) = ID (2 bytes) + Flags (2 bytes) + QDCOUNT (2 bytes) + ANCOUNT (2 bytes) + NSCOUNT (2 bytes) + ARCOUNT (2 bytes)
QUESTION Section = QNAME + QTYPE + QCLASS
ANSWER Section = NAME + TYPE (2 bytes) + CLASS (2 bytes) + TTL (4 bytes) + RDLENGTH (2 bytes) + RDATA (RDLENGTH bytes)
-----------------------*/

/* DNS header struct (12 bytes fixed) */
struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
};

/* Function prototypes */
int validate_ip(const char *ip);
int create_udp_socket();
int build_dns_query(unsigned char *query, size_t query_size, const char *domain, uint16_t query_id);
void encode_domain_name(unsigned char *buffer, const char *domain);int send_dns_query(int sockfd, const char *dns_server_ip, const unsigned char *query, int query_len);
int send_dns_query(int sockfd, const char *dns_server_ip, const unsigned char *query, int query_len);
int receive_dns_response(int sockfd, unsigned char *response, size_t response_size);
int parse_dns_response(const unsigned char *response, int response_len, uint16_t expected_id);
int skip_dns_name(const unsigned char *message, int messsage_len, int pos);


int main(int argc, char *argv[]) {
    /* Validate command-line arguments. */
    if (argc != 3) {
        fprintf(stderr, "Using command: %s <dns_server_ip> <domain_name>\n"
            "Example: %s 8.8.8.8 www.example.com\n", argv[0], argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *dns_server_ip = argv[1];
    const char *domain_name = argv[2];
    int sockfd = -1;
    unsigned char query[QUERY_BUFFER];
    unsigned char response[RESPONSE_BUFFER];
    int query_len, response_len, found;

    if (validate_ip(dns_server_ip) == 0) {
        fprintf(stderr, "[ERROR] Invalid DNS Server IP Address");
        exit(EXIT_FAILURE);
    }

    /* Create UDP socket. */
    sockfd = create_udp_socket(dns_server_ip);

    /* Build UDP Packet */
    query_len = build_dns_query(query, sizeof(query), domain_name, QUERY_ID);

    /* Send DNS query to server */
    if (send_dns_query(sockfd, dns_server_ip, query, query_len) < 0) {
        close(sockfd);
        return EXIT_FAILURE;
    }

    /* Receive DNS response from server */
    response_len = receive_dns_response(sockfd, response, sizeof(response));
    if (response_len < 0) {
        close(sockfd);
        return EXIT_FAILURE;
    }

    /* Parse DNS response and print IPv4 addresses */
    found = parse_dns_response(response, response_len, QUERY_ID);
    if (found <= 0) {
        printf("[DNS] No IPv4 address found for: %s\n", domain_name);
    }

    close(sockfd);
    return EXIT_SUCCESS;
}

int validate_ip(const char *ip) {
    struct in_addr addr;

    if (inet_pton(AF_INET, ip, &addr) <= 0) {
        return 0;
    }

    return 1;
}

int create_udp_socket() {
    int sockfd;
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

int build_dns_query(unsigned char *query, size_t query_size, const char *domain, uint16_t query_id) {
    struct dns_header *header;
    unsigned char *ptr;
    uint16_t qtype;
    uint16_t qclass;
    int query_len = -1;
    memset(query, 0, query_size);

    /* DNS header */
    header = (struct dns_header *)query;
    header->id = htons(query_id);
    header->flags = htons(0x0100);
    header->qdcount = htons(1);
    header->ancount = 0;
    header->nscount = 0;
    header->arcount = 0;

    /* DNS Question section: QNAME, QTYPE, QCLASS */
    /* Encode domain name into QNAME */
    ptr = query + sizeof(struct dns_header);
    encode_domain_name(ptr, domain);
    ptr += strlen(domain) + 2;

    /* QTYPE */
    qtype = htons(TYPE_A);
    memcpy(ptr, &qtype, sizeof(qtype));
    ptr += sizeof(qtype);

    /* QCLASS */
    qclass = htons(CLASS_IN);
    memcpy(ptr, &qclass, sizeof(qclass));
    ptr += sizeof(qclass);

    query_len = (int)(ptr - query);

    return query_len;
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

int send_dns_query(int sockfd, const char *dns_server_ip, const unsigned char *query, int query_len) {
    ssize_t sent;
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    /* Configure server address */
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DNS_PORT);
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

    printf("[DNS] Query sent to %s:%d\n", dns_server_ip, DNS_PORT);
    return 0;
}

int receive_dns_response(int sockfd, unsigned char *response, size_t response_size) {
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);
    ssize_t received;

    /* Receive DNS answer */
    received = recvfrom(sockfd, response, response_size, 0, (struct sockaddr *)&server_addr, &addr_len);
    if (received < 0) {
        perror("recvfrom");
        return -1;
    }

    printf("[DNS] Response received\n");
    return (int)received;
}


/* -------------------- 
DNS Message = Header + QUESTION Section + ANSWER Section + AUTHORITY Section + ADDITIONAL Section 
Header (12 bytes) = ID (2 bytes) + Flags (2 bytes) + QDCOUNT (2 bytes) + ANCOUNT (2 bytes) + NSCOUNT (2 bytes) + ARCOUNT (2 bytes)
QUESTION Section = QNAME + QTYPE + QCLASS
ANSWER Section = NAME + TYPE (2 bytes) + CLASS (2 bytes) + TTL (4 bytes) + RDLENGTH (2 bytes) + RDATA (RDLENGTH bytes)
-----------------------*/
int parse_dns_response(const unsigned char *response, int response_len, uint16_t expected_id) {
    struct dns_header *header;
    uint16_t response_id;
    uint16_t ancount;
    uint16_t qdcount;
    int pos;
    int found;

    if (response_len < (int)sizeof(struct dns_header)) {
        fprintf(stderr, "[ERROR] Response too short\n");
        return -1;
    }

    /* Read DNS header from response */
    header = (struct dns_header *)response;

    /* Verify Response ID */
    response_id = ntohs(header->id);
    if (response_id != expected_id) {
        fprintf(stderr, "[ERROR] Response ID mismatch (expected 0x%04X, got 0x%04X)\n", expected_id, response_id);
        return -1;
    }

    /* Read number of answer records */
    ancount = ntohs(header->ancount);
    if (ancount <= 0) {
        printf("[DNS] No answer records found\n");
        return 0;
    }

    printf("[DNS] Answer records: %d\n", ancount);

    /* Skip DNS Header */
    pos = sizeof(struct dns_header);

    /* Skip QUESTION Section */
    qdcount = ntohs(header->qdcount);
    for (int q = 0; q < qdcount; q++) {
        /* Skip QNAME */
        pos = skip_dns_name(response, response_len, pos);
        if (pos < 0) {
            fprintf(stderr, "[ERROR] Failed to skip question %d\n", q);;
            return -1;
        }

        /* Skip QTYPE and QCLASS */
        pos += 4;
    }   

    /* ANSWER Section */
    found = 0;
    for (int i = 0; i < ancount; i++) {
        uint16_t type;
        uint16_t class_code;
        uint16_t rdlength;
        char ip_str[INET_ADDRSTRLEN];

        if (pos >= response_len) {
            fprintf(stderr, "[ERROR] Response truncated at answer %d\n", i);
            break;
        }

        /* Skip NAME */
        pos = skip_dns_name(response, response_len, pos);
        if (pos < 0) {
            fprintf(stderr, "[ERROR] Failed to skip NAME in ANSWER %d\n", i);
            break;
        }

        /* TYPE + CLASS + TTL + RDLENGTH */
        if (pos + 10 > response_len) {
            fprintf(stderr, "[ERROR] Answer record %d too short\n", i);
            break;
        }

        /* Read TYPE (2 bytes) */
        memcpy(&type, response + pos, 2);
        type = ntohs(type);
        pos += 2;

        /* Read CLASS (2 bytes) */
        memcpy(&class_code, response + pos, 2);
        class_code = ntohs(class_code);
        pos += 2;

        /* Skip TTL */
        pos += 4;

        /* Read RDLENGTH */
        memcpy(&rdlength, response + pos, 2);
        rdlength = ntohs(rdlength);
        pos += 2;

        /* Check TYPE = 1, CLASS = 1, RDLENGTH = 4 */
        if  (type == TYPE_A && class_code == CLASS_IN && rdlength == 4) {
            if  (pos + 4 > response_len) {
                fprintf(stderr, "[ERROR] RDATA truncated in answer %d\n", i);
                break;
            }

            inet_ntop(AF_INET, response + pos, ip_str, sizeof(ip_str));
            printf("[OK] IPv4 address: %s\n", ip_str);
            found ++;
        }

        pos += rdlength;
    }

    return found;

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