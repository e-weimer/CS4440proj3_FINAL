# Makefile for Project 3

CC      := gcc
CFLAGS  := -Wall -Wextra -g

# All binaries built by default
PROGS   := server client ls_server ls_client

all: $(PROGS)

# ---------------- Problem 1 programs ----------------

# Basic reverse-string server (multi-threaded)
server: server.c
	$(CC) $(CFLAGS) -pthread -o $@ $<

# Basic reverse-string client
client: client.c
	$(CC) $(CFLAGS) -o $@ $<

# ---------------- Problem 2 programs ----------------

# Directory listing server that forks and execs ls
ls_server: ls_server.c
	$(CC) $(CFLAGS) -o $@ $<

# Directory listing client that sends ls arguments
ls_client: ls_client.c
	$(CC) $(CFLAGS) -o $@ $<

# ---------------- Convenience run targets ----------------

run-server: server
	./server 5555

run-client: client
	./client 127.0.0.1 5555 "hello world from client"

run-ls-server: ls_server
	./ls_server 5560

run-ls-client: ls_client
	./ls_client 127.0.0.1 5560 .

# ---------------- Housekeeping ----------------

clean:
	rm -f $(PROGS)

.PHONY: all clean run-server run-client run-ls-server run-ls-client
