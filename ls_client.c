/* This program implements a TCP client that connects to a directory-listing server.
It sends "ls" command arguments constructed from command-line arguments to the server,
then receives and prints the directory listing output sent back by the server.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUF 8192   // buffer size for outgoing and incoming data

/*
 * main function that:
 *  - validates arguments
 *  - connects to the server over TCP
 *  - sends the "ls" command arguments as a single line
 *  - receives the output from the remote ls and prints it
 *  - handles all error cases and exits with an appropriate status
 *
 * tt uses if and while statements to handle errors during connection,
 * sending, and receiving data
 */
int main(int argc, char **argv) {
    // need at least: program, host, port, and one ls-argument
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <host> <port> <ls-args...>\n", argv[0]);
        return 1;
    }

    char *host = argv[1];
    int port = atoi(argv[2]);

    // basic validation of port
    if (port <= 0) {
        fprintf(stderr, "Invalid port\n");
        return 1;
    }

    /*
     * build the payload string to send to the server.
     * The server expects a single line containing all ls arguments
     * separated by spaces, ending with a newline
     */
    char payload[BUF];
    payload[0] = '\0';

    for (int i = 3; i < argc; ++i) {
        // append each argument, leaving room for spaces/newline/terminator
        strncat(payload, argv[i], sizeof(payload) - strlen(payload) - 1);
        if (i < argc - 1) {
            strncat(payload, " ", sizeof(payload) - strlen(payload) - 1);
        }
    }
    // add newline to mark the end of the argument list
    strncat(payload, "\n", sizeof(payload) - strlen(payload) - 1);

    
    // create TCP socket
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("socket");
        return 1;
    }

    
    // prepare the server address structure
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(port);

    // convert host string (e.g., "127.0.0.1") to binary address
    if (inet_pton(AF_INET, host, &sa.sin_addr) <= 0) {
        perror("inet_pton");
        close(s);
        return 1;
    }

    // connect to the server
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("connect");
        close(s);
        return 1;
    }

   
    // send the payload reliably using a loop to handle partial sends
    ssize_t need = (ssize_t)strlen(payload);
    ssize_t sent = 0;

    while (sent < need) {
        ssize_t w = send(s, payload + sent, need - sent, 0);
        if (w <= 0) {             // error or connection closed unexpectedly
            perror("send");
            close(s);
            return 1;
        }
        sent += w;
    }

    /*
     * receive the ls output from the server and stream it to stdout
     * keep reading until recv() returns 0 (server closed connection)
     * or a negative value (error)
     */
    char buf[BUF];
    ssize_t r;

    while ((r = recv(s, buf, sizeof(buf), 0)) > 0) {
        // write exactly r bytes to stdout
        fwrite(buf, 1, (size_t)r, stdout);
    }

    // if recv() failed, report the error and exit with non-zero status
    if (r < 0) {
        perror("recv");
        close(s);
        return 1;   // <-- non-zero on error
    }

    close(s);
    return 0;
}

