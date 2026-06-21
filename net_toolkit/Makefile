CC=gcc
CFLAGS=-Wall -Wextra -O2

SRC=src/main.c \
    src/dns_module.c \
    src/http_module.c \
    src/ftp_module.c \
    src/smtp_module.c \
    src/pop3_module.c

OBJ=$(SRC:.c=.o)
TARGET=net_toolkit

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

clean:
	rm -f src/*.o $(TARGET) downloaded_test.txt net_toolkit.log \
        report.txt
