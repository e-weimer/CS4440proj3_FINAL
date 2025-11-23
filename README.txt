CS4440project3
Project 3 – Socket-based Disk and Filesystem Server

This project is organized into five problems that build on each other:

1. Basic multithreaded echo server.
2. Directory listing server that wraps `ls`.
3. Simulated disk server.
4. Flat filesystem server on top of the disk.
5. Directory structure client (mkdir/cd/pwd/rmdir) on top of the filesystem.

-------------------------------------------------------------------------------------

FILES

Common

- Makefile
  Builds all programs for Problems 1–5.

- Project 3 Report.pdf
  Written user + technical report for the entire project.

-------------------------------------------------------------------------------------

Problem 1 – Basic Client/Server

- server.c
  Multithreaded TCP server that reverses strings from clients.

- client.c
  Interactive TCP client that sends lines to server and prints the reversed reply.

- test_server.sh
  Script to start server and run multiple client requests (including concurrent ones).

- test_client.sh
  Script to exercise client with various inputs.

- server_tests.typescript
  Transcript of test_server.sh run.

- client_tests.typescript
  Transcript of test_client.sh run.

-------------------------------------------------------------------------------------

Problem 2 – Directory Listing Server (ls wrapper)

- ls_server.c
  TCP server that forks and execs the ls program with parameters from clients and returns its output.

- ls_client.c
  Client that sends ls arguments to ls_server and prints the ls output.

- test_ls_server.sh
  Script to exercise ls_server with a variety of ls flags and paths.

- test_ls_client.sh
  Script to exercise ls_client and validate output formatting.

- ls_server_tests.typescript
  Transcript of test_ls_server.sh run.

- ls_client_tests.typescript
  Transcript of test_ls_client.sh run.

-------------------------------------------------------------------------------------

Problem 3 – Basic Disk-Storage System

- disk_server.c
  TCP server that simulates a disk using cylinders and sectors, storing 128-byte sectors in a backing file.

- disk_cli.c
  Command-line client that sends disk commands (I, R c s, W c s data) interactively.

- disk_rand.c
  Random workload client that queries disk size and issues a randomized sequence of reads and writes.

- test_disk_server.sh
  Script that starts disk_server and uses disk_cli to test typical and boundary disk operations.

- test_disk_cli.sh
  Script focused on disk_cli parsing and error handling.

- test_disk_rand.sh
  Script that runs disk_rand with a fixed seed and request count.

- disk_server_tests.typescript
  Transcript of test_disk_server.sh.

- disk_cli_tests.typescript
  Transcript of test_disk_cli.sh.

- disk_rand_tests.typescript
  Transcript of test_disk_rand.sh.

-------------------------------------------------------------------------------------

Problem 4 – File System Server (Flat Filesystem)

- fs_server.c
  Filesystem server that sits on top of disk_server and implements:
    F – format filesystem.
    C f – create file f.
    D f – delete file f.
    L b – list directory (names only or names + metadata).
    R f – read entire file f (returns code len data).
    W f l data – overwrite file f with l bytes of data.

- fs_cli.c
  Filesystem client that sends filesystem commands to fs_server and prints status codes and data.

- test_fs_server.sh
  Script that starts disk_server and fs_server and runs a suite of filesystem operations.

- test_fs_cli.sh
  Script to validate fs_cli behavior and R/W formatting.

- fs_server_tests.typescript
  Transcript of test_fs_server.sh.

- fs_cli_tests.typescript
  Transcript of test_fs_cli.sh.

-------------------------------------------------------------------------------------

Problem 5 – Directory Structure (Client-Side)

- fs_dirs.c
  Directory client that provides:
    mkdir dirname – create directory.
    cd dirname – change current working directory.
    pwd – print current working directory.
    rmdir dirname – remove directory (error if not present or not empty).
  Directories are represented as zero-length “marker” files whose names end with / in the flat filesystem.

- test_fs_dirs.sh
  Script that starts disk_server and fs_server, formats the filesystem, and runs several fs_dirs sessions to test directory creation, navigation, and removal.

- fs_dirs_tests.typescript
  Transcript of test_fs_dirs.sh.

-------------------------------------------------------------------------------------

BUILD

Using the Makefile:

  make        # build all executables
  make clean  # remove all binaries and intermediates

Individual targets (optional):

  make server client
  make ls_server ls_client
  make disk_server disk_cli disk_rand
  make fs_server fs_cli
  make fs_dirs

-------------------------------------------------------------------------------------

RUN

Below are typical commands; see the .sh scripts for the full sequences used in the .typescript logs.

Problem 1

  # In one terminal
  ./server <port>

  # In another terminal
  ./client 127.0.0.1 <port>

Problem 2

  # Start ls_server
  ./ls_server <port>

  # Run a client request
  ./ls_client 127.0.0.1 <port> -l .

Problem 3

  # Start disk_server
  ./disk_server <disk_port> <cylinders> <sectors> <track_delay_us> disk.img

  # Interactive client
  ./disk_cli 127.0.0.1 <disk_port>

  # Random workload
  ./disk_rand 127.0.0.1 <disk_port> <seed> <num_requests>

Problem 4

  # Start disk and filesystem servers
  ./disk_server 5600 32 32 1000 disk.img
  ./fs_server 5601 127.0.0.1 5600

  # Filesystem client
  ./fs_cli 127.0.0.1 5601

Problem 5

  # Directory client (assumes fs_server as above)
  ./fs_dirs 127.0.0.1 5601

-------------------------------------------------------------------------------------

