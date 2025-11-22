/*
 * Simple TCP client for the reverse-string server.
 *
 * - Connects to a given host and port.
 * - Sends the command-line string arguments as a single line.
 * - Receives the reversed line from the server and prints it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUF 8192

// main function that sets up the client, connects to the server,
// sends the string to be reversed, and receives the reversed string
// it uses if and while statements to handle errors during connection,
// sending, and receiving data
int main(int argc, char **argv) {

    // check that we have at least: program, host, port, and one word
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <host> <port> <string...>\n", argv[0]);
        return 1;
    }

    char *host = argv[1];
    int port = atoi(argv[2]);

    // basic validation for the port
    if (port <= 0) {
        fprintf(stderr, "Invalid port\n");
        return 1;
    }

    // build a single output string from argv[3..]
    // this concatenates all remaining arguments with spaces in between
    char out[BUF] = {0};
    for (int i = 3; i < argc; ++i) {
        // strncat prevents buffer overflow: we always leave room for '\0'
        strncat(out, argv[i], sizeof(out) - strlen(out) - 1);
        if (i < argc - 1) {
            strncat(out, " ", sizeof(out) - strlen(out) - 1);
        }
    }

    // add a newline so the server can treat this as a line of input
    strncat(out, "\n", sizeof(out) - strlen(out) - 1);

    // create a TCP socket
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("socket");
        return 1;
    }

    // fill in server address structure
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);

    // convert dotted-decimal host string to binary address
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

    // send the entire request string to the server, handling partial sends
    ssize_t sent = 0;
    ssize_t need = strlen(out);
    while (sent < need) {
        ssize_t w = send(s, out + sent, need - sent, 0);
        if (w <= 0) {
            // send() error or connection closed unexpectedly
            perror("send");
            close(s);
            return 1;
        }
        sent += w;
    }

    // receive the response from the server
    char in[BUF];
    ssize_t r = recv(s, in, sizeof(in) - 1, 0);

    // Check the result of recv to see if data was received,
    // the connection was closed, or an error occurred
    if (r > 0) {
        // Normal case: we got some bytes back from the server.
        in[r] = '\0';
        printf("%s", in);
        close(s);
        return 0;
    } else if (r == 0) {
        // The server closed the connection cleanly without sending any data.
        fprintf(stderr, "Server closed connection\n");
        close(s);
        return 1;
    } else {
        // A real error occurred during recv(); print the error and return non-zero.
        perror("recv");
        close(s);
        return 1;
    }
}

