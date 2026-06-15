CC     = gcc
CFLAGS = -Wall -Wextra -O2

all: dns_client

dns_client: dns_client.c
	$(CC) $(CFLAGS) -o dns_client dns_client.c

clean:
	rm -f dns_client