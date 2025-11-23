// Basic simulated disk server for Problem 3.
//
// This program implements a TCP server that behaves like a very simple
// block device.  The "disk" is organized by cylinder and sector, with
// a fixed block size of 128 bytes.  All disk data is stored in a real
// backing file using mmap(2), so the contents persist across runs.
//
// Protocol (all numbers are ASCII decimal separated by spaces):
//
//   I
//     -> disk replies with: "<cylinders> <sectors>\n"
//
//   R c s
//     -> if (c,s) is valid: disk replies with '1' followed by 128 bytes
//        from that sector.
//        otherwise: disk replies with '0'
//
//   W c s l <l raw bytes>
//     -> if (c,s) valid and 0 <= l <= 128: disk replies with '1'
//        after writing l bytes to the front of the sector and filling
//        the remaining bytes with zeros.
//        otherwise: disk replies with '0'
//
// The server simulates track-to-track seek time using nanosleep().
// The number of cylinders, sectors, track time (microseconds) and
// the backing file name are all command-line arguments.
//
// One thread is created per client connection.  All threads share a
// single "disk arm" position protected by a mutex so that seeks are
// serialized across clients.

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BLKSZ      128        // bytes per sector
#define MAX_LINE   1024       // max command line length from client
#define BACKLOG    16         // listen() backlog

// -------- global state describing the simulated disk --------

// disk geometry (set from command line)
static long g_cyl       = 0;   // number of cylinders
static long g_sec       = 0;   // number of sectors per cylinder
static long g_track_us  = 0;   // track-to-track seek time in microseconds

// backing file and mmap'ed region
static int   g_fd       = -1;  // backing file descriptor
static unsigned char *g_base = NULL; // base of mmap()'d disk image

// simulated disk arm position and mutex to protect it
static long g_head_cyl = 0;    // current cylinder
static pthread_mutex_t g_arm_mtx = PTHREAD_MUTEX_INITIALIZER;

// flag used to request termination from SIGINT
static volatile sig_atomic_t g_stop = 0;

// simple struct to pass the accepted client fd into the worker thread
typedef struct {
    int client_fd;
} client_arg_t;

// ------------- helper functions for I/O on sockets -------------

// read exactly n bytes from fd into buf, unless EOF or error
// returns n on success, 0 on clean EOF, or -1 on error
static ssize_t read_exact(int fd, void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = recv(fd, (char *)buf + off, n - off, 0);
        if (r == 0) {
            // peer closed connection.
            return 0;
        }
        if (r < 0) {
            if (errno == EINTR) {
                continue; // interrupted by signal, retry
            }
            return -1;    // real error
        }
        off += (size_t)r;
    }
    return (ssize_t)off;
}

// write exactly n bytes from buf to fd
// returns n on success, or -1 on error
static ssize_t write_all(int fd, const void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = send(fd, (const char *)buf + off, n - off, 0);
        if (w < 0) {
            if (errno == EINTR) {
                continue; // interrupted by signal, retry
            }
            return -1;
        }
        off += (size_t)w;
    }
    return (ssize_t)off;
}

// read a single '\n'-terminated line from fd into out
// out is always NUL-terminated as long as MAX_LINE > 0
//
// returns number of bytes stored (excluding NUL) on success,
// 0 on EOF, or -1 on error
static ssize_t readline(int fd, char *out) {
    size_t off = 0;

    while (off + 1 < MAX_LINE) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r == 0) {
            // EOF with no more data
            break;
        }
        if (r < 0) {
            if (errno == EINTR) {
                continue; // retry on signal
            }
            return -1;
        }

        out[off++] = c;
        if (c == '\n') {
            break;        // end of line
        }
    }

    out[off] = '\0';
    return (ssize_t)off;
}

// ------------- disk geometry / timing helpers -------------

// compute a pointer to the start of the block at cylinder c, sector s
static unsigned char *blk_ptr(long c, long s) {
    // index of sector in the linear disk image
    size_t idx = (size_t)c * (size_t)g_sec + (size_t)s;
    return g_base + idx * BLKSZ;
}

// validate that cylinder c and sector s are within the disk geometry
static int validate_cs(long c, long s) {
    return (c >= 0 && c < g_cyl && s >= 0 && s < g_sec);
}

// simulate the time required to move the head from "from" to "to"
// assume constant track-to-track delay (g_track_us microseconds)
static void sleep_tracks(long from, long to) {
    long delta = labs(to - from);       // number of tracks traveled
    long total_us = delta * g_track_us; // total microseconds

    if (total_us <= 0) {
        return; // no movement or zero delay configured
    }

    struct timespec ts;
    ts.tv_sec  = total_us / 1000000L;
    ts.tv_nsec = (total_us % 1000000L) * 1000L;

    // ignore EINTR here; a partial sleep is good enough for simulation
    nanosleep(&ts, NULL);
}

// ------------- command handlers for the disk protocol -------------

// handle the "I" command: return "<cyl> <sec>\n"
static int handle_I(int fd) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%ld %ld\n", g_cyl, g_sec);
    if (n < 0) {
        return -1;
    }
    return (write_all(fd, buf, (size_t)n) == n) ? 0 : -1;
}

// handle "R c s": read a 128-byte sector
//
// on invalid (c,s): send '0'
// on valid (c,s): simulate seek, then send '1' followed by 128 bytes
static int handle_R(int fd, long c, long s) {
    if (!validate_cs(c, s)) {
        char z = '0';
        return (write_all(fd, &z, 1) == 1) ? 0 : -1;
    }

    // serialize access to the disk arm and sector
    pthread_mutex_lock(&g_arm_mtx);

    long from = g_head_cyl;
    sleep_tracks(from, c);     // simulate seek time
    g_head_cyl = c;

    unsigned char *p = blk_ptr(c, s);
    char ok = '1';
    int rv = 0;

    if (write_all(fd, &ok, 1) != 1) {
        rv = -1;
    } else if (write_all(fd, p, BLKSZ) != BLKSZ) {
        rv = -1;
    }

    pthread_mutex_unlock(&g_arm_mtx);
    return rv;
}

// handle "W c s l" followed by l raw bytes
//
//    - on invalid cylinder/sector or bad length, send '0'
//    - on success, write the data into the mapped disk sector
//      (zero-filling any remaining bytes) and send '1'
static int handle_W(int fd, long c, long s, long l) {
    unsigned char buf[BLKSZ];

    if (!validate_cs(c, s) || l < 0 || l > BLKSZ) {
        char z = '0';
        return (write_all(fd, &z, 1) == 1) ? 0 : -1;
    }

    // read exactly l bytes of raw data from the client
    if (l > 0) {
        if (read_exact(fd, buf, (size_t)l) != l) {
            return -1;
        }
    }

    // zero-fill the remaining bytes in the sector
    if (l < BLKSZ) {
        memset(buf + l, 0, (size_t)(BLKSZ - l));
    }

    pthread_mutex_lock(&g_arm_mtx);

    long from = g_head_cyl;
    sleep_tracks(from, c); // simulate seek into position
    g_head_cyl = c;

    // copy the new sector contents into the mapped disk
    memcpy(blk_ptr(c, s), buf, BLKSZ);

    pthread_mutex_unlock(&g_arm_mtx);

    char ok = '1';
    return (write_all(fd, &ok, 1) == 1) ? 0 : -1;
}

// ------------- per-client worker thread -------------

static void *client_main(void *arg) {
    client_arg_t *carg = (client_arg_t *)arg;
    int fd = carg->client_fd;
    free(carg);

    char line[MAX_LINE];

    for (;;) {
        // read a single command line from the client
        ssize_t n = readline(fd, line);
        if (n <= 0) {
            // EOF or error: terminate this connection
            break;
        }

        // ignore pure blank lines
        if (line[0] == '\n' || line[0] == '\0') {
            continue;
        }

        if (line[0] == 'I') {
            if (handle_I(fd) < 0) {
                break;
            }
        } else if (line[0] == 'R') {
            long c, s;
            if (sscanf(line, "R %ld %ld", &c, &s) != 2) {
                // malformed command: close connection
                break;
            }
            if (handle_R(fd, c, s) < 0) {
                break;
            }
        } else if (line[0] == 'W') {
            long c, s, l;
            if (sscanf(line, "W %ld %ld %ld", &c, &s, &l) != 3) {
                break;
            }
            if (handle_W(fd, c, s, l) < 0) {
                break;
            }
        } else {
            // unknown command; stop talking to this client
            break;
        }
    }

    close(fd);
    return NULL;
}

// ------------- signal handler and main program -------------

// SIGINT handler: set a flag to tell the accept loop to exit
static void on_sigint(int signo) {
    (void)signo;
    g_stop = 1;
}

int main(int argc, char **argv) {
    // usage: ./disk_server <port> <cylinders> <sectors> <track_us> <backing_file>
    if (argc != 6) {
        fprintf(stderr,
                "Usage: %s <port> <cylinders> <sectors> <track_us> <backing_file>\n",
                argv[0]);
        return 2;
    }

    int  port = atoi(argv[1]);
    g_cyl      = strtol(argv[2], NULL, 10);
    g_sec      = strtol(argv[3], NULL, 10);
    g_track_us = strtol(argv[4], NULL, 10);
    const char *path = argv[5];

    if (g_cyl <= 0 || g_sec <= 0) {
        fprintf(stderr, "cylinders and sectors must both be > 0\n");
        return 2;
    }

    size_t total_bytes = (size_t)g_cyl * (size_t)g_sec * BLKSZ;

    // open (or create) the backing file for the simulated disk
    g_fd = open(path, O_RDWR | O_CREAT, 0644);
    if (g_fd < 0) {
        perror("open");
        return 1;
    }

    // ensure the file is the right size
    if (ftruncate(g_fd, (off_t)total_bytes) < 0) {
        perror("ftruncate");
        return 1;
    }

    // map the file into memory so sectors are just pointer arithmetic
    g_base = mmap(NULL, total_bytes,
                  PROT_READ | PROT_WRITE, MAP_SHARED,
                  g_fd, 0);
    if (g_base == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    // create listening socket
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((uint16_t)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(srv, BACKLOG) < 0) {
        perror("listen");
        return 1;
    }

    // arrange to stop on Ctrl-C
    signal(SIGINT, on_sigint);

    fprintf(stderr,
            "[disk_server] port=%d geom=%ldx%ld track=%ldus file=%s\n",
            port, g_cyl, g_sec, g_track_us, path);

    // accept loop: spawn a detached thread per client
    while (!g_stop) {
        struct sockaddr_in cli;
        socklen_t cl = sizeof(cli);

        int cfd = accept(srv, (struct sockaddr *)&cli, &cl);
        if (cfd < 0) {
            if (errno == EINTR && g_stop) {
                // interrupted by SIGINT; exit cleanly
                break;
            }
            if (errno == EINTR) {
                // spurious EINTR, keep accepting
                continue;
            }
            perror("accept");
            break;
        }

        client_arg_t *arg = malloc(sizeof(*arg));
        if (!arg) {
            // allocation failure: close client and continue
            close(cfd);
            continue;
        }
        arg->client_fd = cfd;

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_main, arg) == 0) {
            pthread_detach(tid);
        } else {
            perror("pthread_create");
            close(cfd);
            free(arg);
        }
    }

    // clean up global resources
    close(srv);
    munmap(g_base, total_bytes);
    close(g_fd);

    return 0;
}

