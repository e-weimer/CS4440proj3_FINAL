/* This program extends the earlier server to provide directory listing service.
It implements a TCP server that listens on a specified port for incoming connections.
It accepts connections, forks a child process to handle each client,
executes the "ls" command with arguments received from the client,
and sends the output back to the client.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>


// defines the backlog size basically the maximum number of pending connections
// buf is the temp buffer size for receiving data from clients
#define BACKLOG 16
#define BUF 8192

// this functions prints out an error message and exists the program if a fatal error occurs
static void fatal(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// this function gets called when a SIGCHLD signal is received to reap terminated child processes
// the while loop calls waitpid with WNOHANG to avoid blocking
static void reap_children(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

// main function that sets up the server and handles incoming connections
// it takes the port number as a command line argument and listens for incoming connections using if statements to handle errors 
// it uses while loop to accept connections and forks a child process to handle each client
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    if (port <= 0) { fprintf(stderr, "Invalid port\n"); return 1; }

    struct sigaction sa;
    sa.sa_handler = reap_children;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);

    // this section of our code creates a TCP socket and sets socket options
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) fatal("socket");
    int opt = 1;
    if (setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) fatal("setsockopt");

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons(port);
    if (bind(srv, (struct sockaddr*)&saddr, sizeof(saddr)) < 0) fatal("bind");
    if (listen(srv, BACKLOG) < 0) fatal("listen");

    printf("ls_server: listening on port %d\n", port);

    // main server loop that accepts incoming connections
    while (1) {
        struct sockaddr_in ca;
        socklen_t len = sizeof(ca);
        int fd = accept(srv, (struct sockaddr*)&ca, &len);
        if (fd < 0) {
            perror("accept");
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(fd);
            continue;
        }
        if (pid == 0) {
            
            close(srv);

            
            char buf[BUF];
            ssize_t r = recv(fd, buf, sizeof(buf)-1, 0);
            if (r < 0) {
                perror("recv");
                close(fd);
                exit(1);
            }
            buf[r] = '\0';
            
            char *argv_exec[128];
            int ai = 0;
            argv_exec[ai++] = "ls";
            char *tok = strtok(buf, " \t\r\n");
            while (tok && ai < 127) {
                argv_exec[ai++] = tok;
                tok = strtok(NULL, " \t\r\n");
            }
            argv_exec[ai] = NULL;

            // if statements to redirect stdout and stderr to the client socket
            if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2 stdout"); close(fd); exit(1); }
            if (dup2(fd, STDERR_FILENO) < 0) { perror("dup2 stderr"); close(fd); exit(1); }
            close(fd);

            execvp("ls", argv_exec);
            
            perror("execvp");
            exit(1);
        } else {
            
            close(fd);
            
        }
    }

    close(srv);
    return 0;
}