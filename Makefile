# Makefile for OS socket/disk project (Problems 1, 2, and 3)

CC      := gcc
CFLAGS  := -Wall -Wextra -g

# All binaries built by default
PROGS   := server client ls_server ls_client disk_server disk_cli disk_rand

.PHONY: all clean \
        run-server run-client \
        run-ls-server run-ls-client \
        run-disk-server run-disk-cli run-disk-rand

all: $(PROGS)

# ---------------- Problem 1 programs ----------------

# Basic reverse-string server (multi-threaded)
server: server_v2.c
	$(CC) $(CFLAGS) -pthread -o $@ $<

# Basic reverse-string client
client: client_v2.c
	$(CC) $(CFLAGS) -o $@ $<

# ---------------- Problem 2 programs ----------------

# Directory listing server that forks and execs ls
ls_server: ls_server.c
	$(CC) $(CFLAGS) -o $@ $<

# Directory listing client that sends ls arguments
ls_client: ls_client_v2.c
	$(CC) $(CFLAGS) -o $@ $<

# ---------------- Problem 3 programs ----------------

# Simulated disk server (uses mmap + threads)
disk_server: disk_server_v2.c
	$(CC) $(CFLAGS) -pthread -o $@ $<

# Interactive command-line disk client
disk_cli: disk_cli_v2.c
	$(CC) $(CFLAGS) -o $@ $<

# Random workload generator for the disk server
disk_rand: disk_rand_v2.c
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

run-disk-server: disk_server
	./disk_server 5570 10 20 1000 disk.img

run-disk-cli: disk_cli
	./disk_cli 127.0.0.1 5570

run-disk-rand: disk_rand
	./disk_rand 127.0.0.1 5570 100 12345

# ---------------- Housekeeping ----------------

clean:
	rm -f $(PROGS)
