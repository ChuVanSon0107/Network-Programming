#ifndef SMTP_MODULE_H
#define SMTP_MODULE_H

/* Send an email report through SMTP.
 * Return 0 on success and -1 on error.
 */
int smtp_send_report(const char *server_ip, int server_port,
                     const char *sender, const char *recipient,
                     const char *subject, const char *body);

#endif
