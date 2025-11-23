// Problem 3: Random workload generator for the disk server.
//
// This client connects to disk_server, queries the disk size using
// the "I" command, and then issues N random operations.  Each operation
// is randomly chosen to be either a read (R) or a write (W).  Cylinder
// and sector numbers are picked uniformly from the valid ranges.
// All writes send 128 bytes of random data.
//
// The client prints a single character per request (e.g., 'r' for read,
// 'w' for write) so you can see progress but not be overwhelmed by data.

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define BLKSZ   128
#define MAXLINE 256

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

// read a '\n'-terminated line from fd into out (up to max bytes)
// returns bytes stored (excluding NUL) on success, 0 on EOF, or -1 on error
static ssize_t readline(int fd, char *out, size_t max) {
    size_t off = 0;
    while (off + 1 < max) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r == 0) {
            break;
        }
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        out[off++] = c;
        if (c == '\n') {
            break;
        }
    }
    out[off] = '\0';
    return (ssize_t)off;
}

int main(int argc, char **argv) {
    // usage: ./disk_rand <host> <port> <N> <seed>
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <host> <port> <N> <seed>\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    long N = strtol(argv[3], NULL, 10);
    unsigned int seed = (unsigned int)strtoul(argv[4], NULL, 10);

    if (port <= 0 || N <= 0) {
        fprintf(stderr, "Invalid port or N\n");
        return 1;
    }

    srand(seed);

    // create TCP socket
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((uint16_t)port);

    if (inet_pton(AF_INET, host, &sa.sin_addr) <= 0) {
        perror("inet_pton");
        close(s);
        return 1;
    }

    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("connect");
        close(s);
        return 1;
    }

    // first query the disk geometry via the "I" command
    const char *Iq = "I\n";
    if (write_all(s, Iq, strlen(Iq)) < 0) {
        perror("send(I)");
        close(s);
        return 1;
    }

    char line[MAXLINE];
    if (readline(s, line, sizeof(line)) <= 0) {
        fprintf(stderr, "Failed to read geometry\n");
        close(s);
        return 1;
    }

    long cyl = 0, sec = 0;
    if (sscanf(line, "%ld %ld", &cyl, &sec) != 2 || cyl <= 0 || sec <= 0) {
        fprintf(stderr, "Bad geometry: %s\n", line);
        close(s);
        return 1;
    }

    fprintf(stderr, "[disk_rand] geometry: %ld cylinders x %ld sectors\n", cyl, sec);

    unsigned char buf[BLKSZ];

    // generate N random requests
    for (long i = 0; i < N; i++) {
        long c = rand() % cyl;
        long sc = rand() % sec;
        int is_write = rand() & 1;

        if (is_write) {
            // prepare random data
            for (int j = 0; j < BLKSZ; j++) {
                buf[j] = (unsigned char)(rand() & 0xff);
            }

            // build and send "W c s 128\n"
            char hdr[64];
            int n = snprintf(hdr, sizeof(hdr), "W %ld %ld 128\n", c, sc);
            if (n < 0 || write_all(s, hdr, (size_t)n) < 0) {
                perror("send(W hdr)");
                break;
            }
            if (write_all(s, buf, BLKSZ) < 0) {
                perror("send(W data)");
                break;
            }

            char code;
            if (read_exact(s, &code, 1) != 1) {
                fprintf(stderr, "Failed to read W reply\n");
                break;
            }

            // show progress: write 'w' for writes
            putchar('w');
        } else {
            // build and send "R c s\n"
            char hdr[64];
            int n = snprintf(hdr, sizeof(hdr), "R %ld %ld\n", c, sc);
            if (n < 0 || write_all(s, hdr, (size_t)n) < 0) {
                perror("send(R)");
                break;
            }

            char code;
            if (read_exact(s, &code, 1) != 1) {
                fprintf(stderr, "Failed to read R reply\n");
                break;
            }

            if (code == '1') {
                // read and discard the 128-byte block
                if (read_exact(s, buf, BLKSZ) != BLKSZ) {
                    fprintf(stderr, "Failed to read R data\n");
                    break;
                }
            }

            // show progress: 'r' for reads
            putchar('r');
        }

        // newline every 64 operations to keep the terminal tidy
        if ((i + 1) % 64 == 0) {
            putchar('\n');
        }
        fflush(stdout);
    }

    putchar('\n');
    close(s);
    return 0;
}

