#ifndef FTP_MODULE_H
#define FTP_MODULE_H

#include <stddef.h>

/* Open control connection and read greeting 220. */
int ftp_connect_to_server(const char *server_ip, int server_port);

/* Log in using USER/PASS and send PWD. */
int ftp_login(int control_sock, const char *username,
              const char *password);

/* Enter PASV mode and return its data address. */
int ftp_enter_passive_mode(int control_sock, char *data_ip,
                           size_t data_ip_size, int *data_port);

/* LIST and RETR operations. */
int ftp_list_files(int control_sock);
int ftp_download_file(int control_sock, const char *remote_file,
                      const char *local_file);

/* Send QUIT and close the control connection. */
int ftp_quit(int control_sock);

#endif
