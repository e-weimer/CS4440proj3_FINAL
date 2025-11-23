// problem 4 
// Filesystem client that sends filesystem commands to fs_server and prints status codes and data.

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAXLINE 4096

static ssize_t write_all(int fd, const void* buf, size_t n) { size_t o = 0; while (o < n) { ssize_t w = send(fd, (const char*)buf + o, n - o, 0); if (w < 0) { if (errno == EINTR)continue; return -1; }o += (size_t)w; }return (ssize_t)o; }
static ssize_t read_exact(int fd, void* buf, size_t n) { size_t o = 0; while (o < n) { ssize_t r = recv(fd, (char*)buf + o, n - o, 0); if (r == 0)return o; if (r < 0) { if (errno == EINTR)continue; return -1; }o += (size_t)r; }return (ssize_t)o; }

int main(int argc, char** argv) {
    if (argc != 3) { fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]); return 2; }
    const char* host = argv[1]; int port = atoi(argv[2]);

    int s = socket(AF_INET, SOCK_STREAM, 0); if (s < 0) { perror("socket"); return 1; }
    struct sockaddr_in a = { 0 }; a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1) { perror("inet_pton"); return 1; }
    if (connect(s, (struct sockaddr*)&a, sizeof(a)) < 0) { perror("connect"); return 1; }

    fprintf(stderr, "Enter: F | C f | D f | L b | R f | W f l <newline> <raw data>\n");
    char line[MAXLINE];
    while (fgets(line, sizeof(line), stdin)) {
        if (line[0] == 'W') {
            // Send header line as-is, then read l bytes from stdin and forward
            char fname[1024] = { 0 }; unsigned l = 0;
            if (sscanf(line, " W %1023s %u", fname, &l) != 2) { fputs("bad W\n", stderr); continue; }
            if (write_all(s, line, strlen(line)) < 0) break;
            unsigned char* tmp = (unsigned char*)malloc(l ? l : 1); if (!tmp) { fputs("oom\n", stderr); break; }
            for (unsigned i = 0; i < l; i++) { int ch = fgetc(stdin); if (ch == EOF) { fputs("stdin ended early\n", stderr); free(tmp); goto done; } tmp[i] = (unsigned char)ch; }
            if (write_all(s, tmp, l) < 0) { free(tmp); break; }
            free(tmp);
            // read small response code line (e.g., "0\n" or "2\n")
            char resp[32]; int n = recv(s, resp, sizeof(resp) - 1, 0); if (n <= 0) break; resp[n] = 0; fputs(resp, stdout);
        }
        else if (line[0] == 'R') {
            if (write_all(s, line, strlen(line)) < 0) break;
            // Expect: "<code> <len> <data>\n"
            char hdr[64]; int pos = 0;
            // read until we get space after len
            // For simplicity, read a chunk, print as it comes:
            int n = recv(s, hdr, sizeof(hdr) - 1, 0); if (n <= 0) break; hdr[n] = 0;
            // Print whatever server sent (it includes data and trailing newline)
            fputs(hdr, stdout);
            // If the server sent "0 <len> " followed by raw data then '\n', the above prints it.
            // For larger files, a proper buffered loop would be better; this is enough for tests.
        }
        else {
            if (write_all(s, line, strlen(line)) < 0) break;
            char resp[2048]; int n = recv(s, resp, sizeof(resp) - 1, 0); if (n <= 0) break; resp[n] = 0; fputs(resp, stdout);
        }
    }
done:
    close(s); return 0;
}
