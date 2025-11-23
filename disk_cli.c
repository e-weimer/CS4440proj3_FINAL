// Problem 3: Interactive command-line client for the disk server.
//
// This program connects to disk_server over TCP and lets the user
// interactively type disk commands in the required protocol format:
//
//   I
//   R c s
//   W c s l
//
// For read (R) commands, the client prints the status code and the
// first part of the data in hex so that it is easy to see what was
// read.  For write (W) commands, the client reads l raw bytes from
// stdin *after* the command line and forwards them directly to the
// server, then prints the status code returned by the server.

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BLKSZ   128
#define MAXLINE 4096

// write exactly n bytes from buf to fd, handling partial writes and EINTR
static ssize_t write_all(int fd, const void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = send(fd, (const char *)buf + off, n - off, 0);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        off += (size_t)w;
    }
    return (ssize_t)off;
}

// read exactly n bytes from fd into buf, handling EINTR and partial reads
// returns n on success, 0 on clean EOF, or -1 on error
static ssize_t read_exact(int fd, void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = recv(fd, (char *)buf + off, n - off, 0);
        if (r == 0) {
            return 0;
        }
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        off += (size_t)r;
    }
    return (ssize_t)off;
}

int main(int argc, char **argv) {
    // usage: ./disk_cli <host> <port>
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    if (port <= 0) {
        fprintf(stderr, "Invalid port\n");
        return 1;
    }

    // create TCP socket
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("socket");
        return 1;
    }

    // build server address structure
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((uint16_t)port);

    if (inet_pton(AF_INET, host, &sa.sin_addr) <= 0) {
        perror("inet_pton");
        close(s);
        return 1;
    }

    // connect to the disk server
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("connect");
        close(s);
        return 1;
    }

    char line[MAXLINE];

    // main interactive loop: read commands from stdin and forward them
    while (fgets(line, sizeof(line), stdin) != NULL) {
        size_t len = strlen(line);
        if (len == 0) {
            continue;
        }

        // send the command line exactly as typed (including newline)
        if (write_all(s, line, len) < 0) {
            perror("send");
            break;
        }

        if (line[0] == 'I') {
            // I: server sends "<cyl> <sec>\n"
            char buf[128];
            ssize_t r = recv(s, buf, sizeof(buf) - 1, 0);
            if (r <= 0) {
                break;
            }
            buf[r] = '\0';
            printf("%s", buf);
        } else if (line[0] == 'R') {
            // R c s: server sends code + optional 128 bytes
            char code;
            if (read_exact(s, &code, 1) != 1) {
                break;
            }
            if (code == '0') {
                // No such block.
                puts("0");
            } else {
                // read 128 bytes and print summary in hex
                unsigned char data[BLKSZ];
                if (read_exact(s, data, BLKSZ) != BLKSZ) {
                    break;
                }
                printf("1 ");
                // show only first 32 bytes to keep output manageable
                for (int i = 0; i < 32; i++) {
                    printf("%02x", data[i]);
                }
                printf(" ...\n");
            }
        } else if (line[0] == 'W') {
            // W c s l: parse the length so we know how many bytes to read
            long c, sn, l;
            if (sscanf(line, "W %ld %ld %ld", &c, &sn, &l) != 3) {
                puts("bad");
                continue;
            }

            unsigned char tmp[BLKSZ];
            memset(tmp, 0, sizeof(tmp));

            // read l raw bytes from stdin and store them in tmp
            for (long i = 0; i < l; i++) {
                int ch = fgetc(stdin);
                if (ch == EOF) {
                    fprintf(stderr, "stdin ended early\n");
                    close(s);
                    exit(1);
                }
                tmp[i] = (unsigned char)ch;
            }

            // send those l bytes to the server
            if (write_all(s, tmp, (size_t)l) < 0) {
                perror("send");
                break;
            }

            // server replies with a single status character
            char code;
            if (read_exact(s, &code, 1) != 1) {
                break;
            }
            printf("%c\n", code);
        } else {
            // unknown command; do nothing special, just continue loop
            fprintf(stderr, "Unknown command type: %c\n", line[0]);
        }
    }

    close(s);
    return 0;
}

