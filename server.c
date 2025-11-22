/* This program implements a TCP server that listens on a specified port for incoming connections.
It accepts the connection and makes a new thread to handle the client server.
The string it gets from tthe client the server reveres and returns to the client.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define BACKLOG 16      // max pending connections in listen queue
#define BUF_SIZE 4096   // buffer size for each client request

// helper: print an error message and exit on fatal errors in main thread
static void fatal(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// struct passed to each worker thread containing the client fd and address
typedef struct {
    int client_fd;
    struct sockaddr_in addr;
} client_arg_t;

// reverse a string in place, using indices [0, n)
static void reverse_inplace(char *s, ssize_t n) {
    ssize_t i = 0, j = n - 1;
    while (i < j) {
        char t = s[i];
        s[i] = s[j];
        s[j] = t;
        ++i;
        --j;
    }
}

// thread entry point for each client connection
static void *thread_main(void *arg) {
    client_arg_t *ca = (client_arg_t *)arg;
    int fd = ca->client_fd;
    free(ca); // we no longer need the struct; only the fd is required

    // this printf is just for debugging/visualization:
    // every new connection will print the thread id and client fd
    // the sleep simulates extra work / slowdown to demonstrate DoS effects
    printf("Thread %lu started for client fd=%d\n", pthread_self(), fd);
    sleep(2);

    // receives data from the client, reverses the string, and sends it back
    char buf[BUF_SIZE];
    ssize_t r = recv(fd, buf, sizeof(buf) - 1, 0);
    if (r <= 0) {
        // If r == 0 the client closed the connection; if r < 0 an error occurred
        if (r < 0) {
            perror("recv");
        }
        close(fd);
        return NULL;
    }

    // if statement checks if the last character received is a newline
    // and removes it before reversing
    // buf[r] makes sure the string is null-terminated before reversing
    if (buf[r - 1] == '\n') {
        --r;
    }
    buf[r] = '\0';

    // reverse the string in place
    reverse_inplace(buf, r);

    // add a newline back to the reversed string
    strncat(buf, "\n", sizeof(buf) - strlen(buf) - 1);

    // send the reversed string back to the client, handling partial sends
    ssize_t tosend = strlen(buf);
    ssize_t sent = 0;
    while (sent < tosend) {
        ssize_t s = send(fd, buf + sent, tosend - sent, 0);
        if (s <= 0) {
            // if send fails, log the error (if any) and break out of the loop
            if (s < 0) {
                perror("send");
            }
            break;
        }
        sent += s;
    }

    close(fd);
    return NULL;
}

// main function that sets up the server, listens for incoming connections,
// and creates threads to handle each client
// it uses if and while statements to handle errors during socket creation,
// binding, listening, and accepting connections
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0) {
        fprintf(stderr, "Invalid port\n");
        return 1;
    }

    // create a TCP socket
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        fatal("socket");
    }

    // allow quick reuse of the port when restarting the server
    int yes = 1;
    if (setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        fatal("setsockopt");
    }

    // bind to the specified port on all interfaces
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);

    if (bind(srv, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        fatal("bind");
    }

    // start listening for incoming connections
    if (listen(srv, BACKLOG) < 0) {
        fatal("listen");
    }

    printf("Server listening on port %d\n", port);

    // main accept loop: each accepted client gets its own thread
    while (1) {
        client_arg_t *ca = malloc(sizeof(*ca));
        if (!ca) {
            // if malloc fails, close and bail out
            fatal("malloc");
        }

        socklen_t len = sizeof(ca->addr);
        ca->client_fd = accept(srv, (struct sockaddr *)&ca->addr, &len);
        if (ca->client_fd < 0) {
            // accept failed; log and retry
            perror("accept");
            free(ca);
            continue;
        }

        // display which client connected (for debugging)
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ca->addr.sin_addr, addr_str, sizeof(addr_str));
        printf("Accepted connection from %s:%d (fd=%d)\n",
               addr_str, ntohs(ca->addr.sin_port), ca->client_fd);

        pthread_t tid;
        if (pthread_create(&tid, NULL, thread_main, ca) != 0) {
            // if fails to create a thread for this client,
            // close its socket and free the argument struct
            perror("pthread_create");
            close(ca->client_fd);
            free(ca);
            continue;
        }

        // detach the thread so we don't have to pthread_join() it later
        pthread_detach(tid);
    }

    close(srv);
    return 0;
}

