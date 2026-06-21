#ifndef POP3_MODULE_H
#define POP3_MODULE_H

/* Find an email whose Subject header matches subject.
 * Return 0 when found and -1 on error or when no email matches.
 */
int pop3_find_email_by_subject(const char *server_ip, int server_port,
                               const char *username, const char *password,
                               const char *subject);

#endif
