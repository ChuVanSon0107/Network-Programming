#include "dns_module.h"
#include "ftp_module.h"
#include "http_module.h"
#include "pop3_module.h"
#include "smtp_module.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WEB_DOMAIN "web.lab.local"
#define FTP_DOMAIN "ftp.lab.local"
#define MAIL_DOMAIN "mail.lab.local"

#define HTTP_PORT 8080
#define FTP_PORT 2121
#define SMTP_PORT 1025
#define POP3_PORT 1110

#define FTP_USERNAME "student"
#define FTP_PASSWORD "student123"
#define MAIL_USERNAME "student"
#define MAIL_PASSWORD "student123"

#define MAIL_SENDER "student@lab.local"
#define MAIL_RECIPIENT "teacher@lab.local"
#define MAIL_SUBJECT "Network Toolkit Report"

struct test_result {
    int dns_web;
    int http;
    int dns_ftp;
    int ftp_login;
    int ftp_list;
    int ftp_retr;
    int dns_mail;
    int smtp;
    int pop3;
};

static const char *result_text(int result) {
    return result == 0 ? "OK" : "FAILED";
}

static int resolve_domain(const char *dns_server_ip, int dns_server_port,
                          const char *domain, char *ip_buffer) {
    if (dns_resolve_a(dns_server_ip, dns_server_port, domain,
                      ip_buffer, INET_ADDRSTRLEN) < 0) {
        printf("[DNS] %s FAILED\n", domain);
        return -1;
    }

    printf("[DNS] %s -> %s OK\n", domain, ip_buffer);
    return 0;
}

static int run_http_test(const char *dns_server_ip, int dns_server_port) {
    char server_ip[INET_ADDRSTRLEN];

    if (resolve_domain(dns_server_ip, dns_server_port,
                       WEB_DOMAIN, server_ip) < 0) {
        return -1;
    }

    if (http_get_status(server_ip, HTTP_PORT,
                        WEB_DOMAIN, "/status") < 0) {
        printf("[HTTP] GET /status FAILED\n");
        return -1;
    }

    printf("[HTTP] GET /status -> 200 OK\n");
    return 0;
}

static int run_ftp_test(const char *dns_server_ip, int dns_server_port,
                        int run_list, int run_download) {
    char server_ip[INET_ADDRSTRLEN];
    int control_sock;
    int result = 0;

    if (resolve_domain(dns_server_ip, dns_server_port,
                       FTP_DOMAIN, server_ip) < 0) {
        return -1;
    }

    control_sock = ftp_connect_to_server(server_ip, FTP_PORT);
    if (control_sock < 0) {
        return -1;
    }

    if (ftp_login(control_sock, FTP_USERNAME, FTP_PASSWORD) < 0) {
        ftp_quit(control_sock);
        return -1;
    }
    printf("[FTP] Login OK\n");

    if (run_list) {
        if (ftp_list_files(control_sock) < 0) {
            printf("[FTP] LIST FAILED\n");
            result = -1;
        } else {
            printf("[FTP] LIST OK\n");
        }
    }

    if (run_download) {
        if (ftp_download_file(control_sock, "test.txt",
                              "downloaded_test.txt") < 0) {
            printf("[FTP] RETR test.txt FAILED\n");
            result = -1;
        } else {
            printf("[FTP] RETR test.txt OK\n");
        }
    }

    ftp_quit(control_sock);
    return result;
}

static void create_report_body(char *report, size_t report_size,
                               const struct test_result *test) {
    snprintf(report, report_size,
             "DNS web.lab.local: %s\r\n"
             "HTTP GET /status: %s\r\n"
             "DNS ftp.lab.local: %s\r\n"
             "FTP LIST: %s\r\n"
             "FTP RETR test.txt: %s\r\n"
             "DNS mail.lab.local: %s\r\n"
             "SMTP send report: OK\r\n"
             "POP3 retrieve report: pending",
             result_text(test->dns_web),
             result_text(test->http),
             result_text(test->dns_ftp),
             result_text(test->ftp_list),
             result_text(test->ftp_retr),
             result_text(test->dns_mail));
}

static int send_default_report(const char *dns_server_ip,
                               int dns_server_port) {
    char server_ip[INET_ADDRSTRLEN];
    const char *report =
        "DNS web.lab.local: OK\r\n"
        "HTTP GET /status: OK\r\n"
        "DNS ftp.lab.local: OK\r\n"
        "FTP LIST: OK\r\n"
        "FTP RETR test.txt: OK\r\n"
        "DNS mail.lab.local: OK\r\n"
        "SMTP send report: OK\r\n"
        "POP3 retrieve report: pending";

    if (resolve_domain(dns_server_ip, dns_server_port,
                       MAIL_DOMAIN, server_ip) < 0) {
        return -1;
    }

    if (smtp_send_report(server_ip, SMTP_PORT, MAIL_SENDER,
                         MAIL_RECIPIENT, MAIL_SUBJECT, report) < 0) {
        printf("[SMTP] Send report FAILED\n");
        return -1;
    }

    printf("[SMTP] Send report OK\n");
    return 0;
}

static int run_pop3_test(const char *dns_server_ip, int dns_server_port) {
    char server_ip[INET_ADDRSTRLEN];

    if (resolve_domain(dns_server_ip, dns_server_port,
                       MAIL_DOMAIN, server_ip) < 0) {
        return -1;
    }

    if (pop3_find_email_by_subject(server_ip, POP3_PORT,
                                   MAIL_USERNAME, MAIL_PASSWORD,
                                   MAIL_SUBJECT) < 0) {
        printf("[POP3] Retrieve report FAILED\n");
        return -1;
    }

    printf("[POP3] Retrieve report OK\n");
    return 0;
}

static void print_test_summary(const struct test_result *test) {
    printf("\n===== Integration Test Summary =====\n");
    printf("[DNS]  web.lab.local          %s\n", result_text(test->dns_web));
    printf("[HTTP] GET /status            %s\n", result_text(test->http));
    printf("[DNS]  ftp.lab.local          %s\n", result_text(test->dns_ftp));
    printf("[FTP]  Login                  %s\n", result_text(test->ftp_login));
    printf("[FTP]  LIST                   %s\n", result_text(test->ftp_list));
    printf("[FTP]  RETR test.txt          %s\n", result_text(test->ftp_retr));
    printf("[DNS]  mail.lab.local         %s\n", result_text(test->dns_mail));
    printf("[SMTP] Send report            %s\n", result_text(test->smtp));
    printf("[POP3] Retrieve report        %s\n", result_text(test->pop3));
}

static int run_full_test(const char *dns_server_ip, int dns_server_port) {
    struct test_result test;
    char web_ip[INET_ADDRSTRLEN];
    char ftp_ip[INET_ADDRSTRLEN];
    char mail_ip[INET_ADDRSTRLEN];
    char report[1024];
    int control_sock = -1;

    memset(&test, -1, sizeof(test));
    printf("===== Full Integration Test =====\n");

    /* 1. Resolve web.lab.local and test HTTP. */
    test.dns_web = resolve_domain(dns_server_ip, dns_server_port,
                                  WEB_DOMAIN, web_ip);
    if (test.dns_web == 0) {
        test.http = http_get_status(web_ip, HTTP_PORT,
                                    WEB_DOMAIN, "/status");
        printf("[HTTP] GET /status %s\n", result_text(test.http));
    }

    /* 2. Resolve ftp.lab.local, login, LIST and RETR. */
    test.dns_ftp = resolve_domain(dns_server_ip, dns_server_port,
                                  FTP_DOMAIN, ftp_ip);
    if (test.dns_ftp == 0) {
        control_sock = ftp_connect_to_server(ftp_ip, FTP_PORT);
        if (control_sock >= 0) {
            test.ftp_login = ftp_login(control_sock,
                                       FTP_USERNAME, FTP_PASSWORD);
            printf("[FTP] Login %s\n", result_text(test.ftp_login));

            if (test.ftp_login == 0) {
                test.ftp_list = ftp_list_files(control_sock);
                printf("[FTP] LIST %s\n", result_text(test.ftp_list));

                test.ftp_retr = ftp_download_file(
                    control_sock, "test.txt", "downloaded_test.txt");
                printf("[FTP] RETR test.txt %s\n",
                       result_text(test.ftp_retr));
            }

            ftp_quit(control_sock);
        }
    }

    /* 3. Resolve mail.lab.local and send report using SMTP. */
    test.dns_mail = resolve_domain(dns_server_ip, dns_server_port,
                                   MAIL_DOMAIN, mail_ip);
    if (test.dns_mail == 0) {
        create_report_body(report, sizeof(report), &test);
        test.smtp = smtp_send_report(mail_ip, SMTP_PORT,
                                     MAIL_SENDER, MAIL_RECIPIENT,
                                     MAIL_SUBJECT, report);
        printf("[SMTP] Send report %s\n", result_text(test.smtp));
    }

    /* 4. Read the report email using POP3. */
    if (test.dns_mail == 0 && test.smtp == 0) {
        test.pop3 = pop3_find_email_by_subject(
            mail_ip, POP3_PORT, MAIL_USERNAME, MAIL_PASSWORD, MAIL_SUBJECT);
        printf("[POP3] Retrieve report %s\n", result_text(test.pop3));
    }

    print_test_summary(&test);

    if (test.dns_web == 0 && test.http == 0 &&
        test.dns_ftp == 0 && test.ftp_login == 0 &&
        test.ftp_list == 0 && test.ftp_retr == 0 &&
        test.dns_mail == 0 && test.smtp == 0 && test.pop3 == 0) {
        printf("\nIntegration test completed successfully.\n");
        return 0;
    }

    printf("\nIntegration test FAILED.\n");
    return -1;
}

static void print_menu(void) {
    printf("\n===== Network Application Client Toolkit =====\n");
    printf("1. Resolve domain using DNS\n");
    printf("2. Send HTTP GET request\n");
    printf("3. Connect FTP and list files\n");
    printf("4. Download file from FTP\n");
    printf("5. Send email report using SMTP\n");
    printf("6. Read email using POP3\n");
    printf("7. Run full integration test\n");
    printf("8. Quit\n");
    printf("Choose an option: ");
}

static void run_menu(const char *dns_server_ip, int dns_server_port) {
    char input[32];
    char domain[256];
    char ip_buffer[INET_ADDRSTRLEN];
    int choice;

    while (1) {
        print_menu();
        if (fgets(input, sizeof(input), stdin) == NULL) {
            return;
        }
        choice = atoi(input);

        switch (choice) {
            case 1:
                printf("Domain: ");
                if (fgets(domain, sizeof(domain), stdin) != NULL) {
                    domain[strcspn(domain, "\r\n")] = '\0';
                    resolve_domain(dns_server_ip, dns_server_port,
                                   domain, ip_buffer);
                }
                break;
            case 2:
                run_http_test(dns_server_ip, dns_server_port);
                break;
            case 3:
                run_ftp_test(dns_server_ip, dns_server_port, 1, 0);
                break;
            case 4:
                run_ftp_test(dns_server_ip, dns_server_port, 0, 1);
                break;
            case 5:
                send_default_report(dns_server_ip, dns_server_port);
                break;
            case 6:
                run_pop3_test(dns_server_ip, dns_server_port);
                break;
            case 7:
                run_full_test(dns_server_ip, dns_server_port);
                break;
            case 8:
                return;
            default:
                printf("Invalid option.\n");
        }
    }
}

static void print_usage(const char *program) {
    fprintf(stderr,
            "Usage: %s (--menu | --full-test) "
            "--dns-server IP --dns-port PORT\n",
            program);
}

int main(int argc, char *argv[]) {
    const char *dns_server_ip = NULL;
    int dns_server_port = 0;
    int menu_mode = 0;
    int full_test_mode = 0;

    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--menu") == 0) {
            menu_mode = 1;
        } else if (strcmp(argv[index], "--full-test") == 0) {
            full_test_mode = 1;
        } else if (strcmp(argv[index], "--dns-server") == 0 &&
                   index + 1 < argc) {
            dns_server_ip = argv[++index];
        } else if (strcmp(argv[index], "--dns-port") == 0 &&
                   index + 1 < argc) {
            dns_server_port = atoi(argv[++index]);
        } else {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if ((menu_mode + full_test_mode) != 1 || dns_server_ip == NULL ||
        dns_server_port <= 0 || dns_server_port > 65535) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (menu_mode) {
        run_menu(dns_server_ip, dns_server_port);
        return EXIT_SUCCESS;
    }

    return run_full_test(dns_server_ip, dns_server_port) == 0
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
