# Makefile for Project 3

# Builds:
#   Problem 1: server, client
#   Problem 2: ls_server, ls_client
#   Problem 3: disk_server, disk_cli, disk_rand
#   Problem 4: fs_server, fs_cli
#   Problem 5: fs_dirs (directory structure client)

CC      = gcc
CFLAGS  = -Wall -Wextra -g -pthread

# Default target: build everything
all: server client \
     ls_server ls_client \
     disk_server disk_cli disk_rand \
     fs_server fs_cli \
     fs_dirs

# --------------------------------------------------------------------
# Problem 1: basic client/server
server: server.c
	$(CC) $(CFLAGS) -o server server.c

client: client.c
	$(CC) $(CFLAGS) -o client client.c

# --------------------------------------------------------------------
# Problem 2: directory listing server (ls)
ls_server: ls_server.c
	$(CC) $(CFLAGS) -o ls_server ls_server.c

ls_client: ls_client.c
	$(CC) $(CFLAGS) -o ls_client ls_client.c

# --------------------------------------------------------------------
# Problem 3: basic disk-storage system
disk_server: disk_server.c
	$(CC) $(CFLAGS) -o disk_server disk_server.c

disk_cli: disk_cli.c
	$(CC) $(CFLAGS) -o disk_cli disk_cli.c

disk_rand: disk_rand.c
	$(CC) $(CFLAGS) -o disk_rand disk_rand.c

# --------------------------------------------------------------------
# Problem 4: file system server
fs_server: fs_server.c
	$(CC) $(CFLAGS) -o fs_server fs_server.c

fs_cli: fs_cli.c
	$(CC) $(CFLAGS) -o fs_cli fs_cli.c

# --------------------------------------------------------------------
# Problem 5: directory structure client (mkdir/cd/pwd/rmdir)
fs_dirs: fs_dirs.c
	$(CC) $(CFLAGS) -o fs_dirs fs_dirs.c

# --------------------------------------------------------------------
# Housekeeping
.PHONY: all clean

clean:
	rm -f server client \
	      ls_server ls_client \
	      disk_server disk_cli disk_rand \
	      fs_server fs_cli fs_dirs \
	      *.o
