CC = gcc
CFLAGS = -Wall -Wextra -g

all: http-server

http-server: http-server.c
	$(CC) $(CFLAGS) -o http-server http-server.c

clean:
	rm -f http-server
