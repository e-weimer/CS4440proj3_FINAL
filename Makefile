# Makefile for OS project (client + server)

CC      := gcc
CFLAGS  := -Wall -Wextra -g

PROGS   := server client

all: $(PROGS)

# Build server directly from server_v2.c
server: server_v2.c
	$(CC) $(CFLAGS) -pthread -o $@ $<

# Build client directly from client_v2.c
client: client_v2.c
	$(CC) $(CFLAGS) -o $@ $<

# Convenience targets

run-server: server
	./server 5555

run-client: client
	./client 127.0.0.1 5555 "hello world"

clean:
	rm -f $(PROGS)

.PHONY: all clean run-server run-client

