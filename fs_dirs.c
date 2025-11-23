/*
 * fs_dirs.c  --  Problem 5: directory structure client
 *
 * This program adds a simple directory structure on top of the flat
 * filesystem implemented in Problem 4 (fs_server / disk_server).
 *
 * Design:
 *   - The underlying filesystem is flat: each entry is just a name and
 *     a byte length.  We emulate directories by reserving names that
 *     end with '/' as directory markers.
 *
 *   - A directory with canonical path "/a/b" is represented by a
 *     zero-length file named "a/b/" in the filesystem.
 *
 *   - The client maintains an in-memory "current working directory"
 *     string (cwd), always starting with '/' (e.g. "/", "/a", "/a/b").
 *     The root directory "/" is implicit and does not need a marker.
 *
 *   - The program connects to fs_server and provides an interactive
 *     shell that accepts:
 *
 *       mkdir dirname    : create a directory named dirname
 *       cd dirname       : change current working directory to dirname
 *       pwd              : print current working directory
 *       rmdir dirname    : remove the directory dirname, if it exists
 *       help             : show available commands
 *       quit / exit      : terminate the client
 *
 *     The dirname argument can be either:
 *       - a simple relative name (no slashes), interpreted relative to cwd
 *         e.g. if cwd = "/a", then "mkdir b" creates "/a/b"
 *       - or an absolute path starting with '/', e.g. "/a/b"
 *
 *   - Implementation of commands:
 *
 *       mkdir dirname
 *         1. Compute canonical path P (string starting with '/', no
 *            trailing slash, except root "/").
 *         2. If P == "/", report error (root already exists).
 *         3. Convert P to a filesystem entry name: fsname = P+1 + "/"
 *            (drop leading '/', append '/').
 *         4. Send "C fsname\n" to fs_server.
 *         5. If server returns "0", success; "1" means already exists;
 *            "2" means some other failure (e.g. no space).
 *
 *       cd dirname
 *         1. Compute canonical path P.
 *         2. If P == "/", succeed immediately.
 *         3. Convert P to fsname = P+1 + "/".
 *         4. Send "R fsname\n" to fs_server.
 *         5. If return code is 0, marker exists and we set cwd = P.
 *            If code is 1, directory does not exist.
 *
 *       pwd
 *         1. Print cwd + '\n'.
 *
 *       rmdir dirname
 *         1. Compute canonical path P (must not be "/").
 *         2. Convert to fsname = P+1 + "/".
 *         3. Send "L 0\n" to fs_server and collect the listing.
 *         4. Parse each line as a filename.  If any filename starts
 *            with fsname and is not equal to fsname itself, then the
 *            directory is not empty and we refuse to remove it.
 *         5. If empty, send "D fsname\n" to fs_server.
 *         6. Return codes: 0 = success, 1 = directory not present,
 *            2 = other failure.
 *
 * Robustness:
 *   - All socket operations (socket(), connect(), send(), recv()) are
 *     checked for errors.  On error, we use perror() to print an
 *     informative message and exit with non-zero status.
 *   - Commands that expect simple numeric status codes verify and
 *     parse them using sscanf().
 *
 * Usage:
 *   Compile (assuming fs_server.c and disk_server.c are already built
 *   and running):
 *
 *       gcc -Wall -Wextra -g -o fs_dirs fs_dirs.c
 *
 *   Run:
 *
 *       ./fs_dirs 127.0.0.1 5601
 *
 *   where 127.0.0.1 and 5601 are the host and port for fs_server.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAXLINE 4096
#define PATHBUF 1024

/* ------------------------------------------------------------------ */
/* Utility: write_all
 *
 * Write exactly n bytes to a socket, handling partial writes and EINTR.
 * Returns n on success, -1 on error (and sets errno).
 */
static ssize_t write_all(int fd, const void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = send(fd, (const char *)buf + off, n - off, 0);
        if (w < 0) {
            if (errno == EINTR) {
                continue;   /* interrupted by signal, retry */
            }
            return -1;
        }
        off += (size_t)w;
    }
    return (ssize_t)off;
}

/* ------------------------------------------------------------------ */
/* Utility: simple command to fs_server
 *
 * Send a single-line command (cmd must include the trailing '\n')
 * and read a response into respbuf, terminated with '\0'.
 *
 * This function is designed for small protocol responses like:
 *   "0\n" or "1\n" or "0 18 Hello, filesystem!\n"
 *
 * Returns number of bytes read (>=0) on success, -1 on error.
 */
static ssize_t fs_simple_cmd(int sock, const char *cmd,
                             char *respbuf, size_t respbuf_sz) {
    size_t len = strlen(cmd);
    if (write_all(sock, cmd, len) < 0) {
        return -1;
    }

    ssize_t r = recv(sock, respbuf, respbuf_sz - 1, 0);
    if (r < 0) {
        return -1;
    }
    respbuf[r] = '\0';
    return r;
}

/* ------------------------------------------------------------------ */
/* Path helpers
 *
 * CWD is always a canonical path starting with '/', with no trailing
 * slash, except root "/" itself.
 */

/* Join cwd and a relative name into a canonical path. */
static void join_path(const char *cwd,
                      const char *name,
                      char *out,
                      size_t out_sz) {
    if (name[0] == '/') {
        /* Absolute path: name already includes leading '/'. */
        snprintf(out, out_sz, "%s", name);
    } else {
        if (strcmp(cwd, "/") == 0) {
            snprintf(out, out_sz, "/%s", name);
        } else {
            snprintf(out, out_sz, "%s/%s", cwd, name);
        }
    }

    /* Remove trailing slashes except for root itself. */
    size_t len = strlen(out);
    while (len > 1 && out[len - 1] == '/') {
        out[len - 1] = '\0';
        len--;
    }
}

/* Convert canonical path like "/a/b" to filesystem name "a/b/". */
static void path_to_fsname(const char *canon_path,
                           char *fsname,
                           size_t fsname_sz) {
    if (strcmp(canon_path, "/") == 0) {
        /* Root directory is implicit; we never create a marker for it. */
        fsname[0] = '\0';
        return;
    }
    /* Skip leading '/', append trailing '/'. */
    snprintf(fsname, fsname_sz, "%s/", canon_path + 1);
}

/* ------------------------------------------------------------------ */
/* mkdir implementation: create directory marker file. */
static void cmd_mkdir(int sock, char *cwd, const char *arg) {
    char path[PATHBUF];
    char fsname[PATHBUF];
    char cmd[MAXLINE];
    char resp[MAXLINE];

    if (arg == NULL || arg[0] == '\0') {
        fprintf(stderr, "mkdir: missing directory name\n");
        return;
    }

    join_path(cwd, arg, path, sizeof(path));
    if (strcmp(path, "/") == 0) {
        fprintf(stderr, "mkdir: cannot create root directory\n");
        return;
    }

    path_to_fsname(path, fsname, sizeof(fsname));
    if (fsname[0] == '\0') {
        fprintf(stderr, "mkdir: internal error (empty fsname)\n");
        return;
    }

    snprintf(cmd, sizeof(cmd), "C %s\n", fsname);
    if (fs_simple_cmd(sock, cmd, resp, sizeof(resp)) < 0) {
        perror("mkdir: send/recv");
        exit(1);
    }

    int code = -1;
    if (sscanf(resp, "%d", &code) != 1) {
        fprintf(stderr, "mkdir: unexpected response: %s", resp);
        return;
    }

    if (code == 0) {
        /* success */
        return;
    } else if (code == 1) {
        fprintf(stderr, "mkdir: '%s' already exists\n", path);
    } else {
        fprintf(stderr, "mkdir: failed with code %d\n", code);
    }
}

/* ------------------------------------------------------------------ */
/* cd implementation: change cwd if directory marker exists. */
static void cmd_cd(int sock, char *cwd, const char *arg) {
    char path[PATHBUF];
    char fsname[PATHBUF];
    char cmd[MAXLINE];
    char resp[MAXLINE];

    if (arg == NULL || arg[0] == '\0') {
        fprintf(stderr, "cd: missing directory name\n");
        return;
    }

    join_path(cwd, arg, path, sizeof(path));

    /* Root always exists. */
    if (strcmp(path, "/") == 0) {
        strcpy(cwd, "/");
        return;
    }

    path_to_fsname(path, fsname, sizeof(fsname));
    if (fsname[0] == '\0') {
        fprintf(stderr, "cd: internal error (empty fsname)\n");
        return;
    }

    /* Use R to check for existence. We only care about the status code. */
    snprintf(cmd, sizeof(cmd), "R %s\n", fsname);
    if (fs_simple_cmd(sock, cmd, resp, sizeof(resp)) < 0) {
        perror("cd: send/recv");
        exit(1);
    }

    int code = -1;
    if (sscanf(resp, "%d", &code) != 1) {
        fprintf(stderr, "cd: unexpected response: %s", resp);
        return;
    }

    if (code == 0) {
        /* Directory marker exists. Update cwd. */
        strncpy(cwd, path, PATHBUF - 1);
        cwd[PATHBUF - 1] = '\0';
    } else if (code == 1) {
        fprintf(stderr, "cd: '%s' does not exist\n", path);
    } else {
        fprintf(stderr, "cd: error code %d while accessing '%s'\n", code, path);
    }
}

/* ------------------------------------------------------------------ */
/* pwd implementation: just print cwd. */
static void cmd_pwd(const char *cwd) {
    printf("%s\n", cwd);
}

/* ------------------------------------------------------------------ */
/* rmdir implementation:
 *   - ensures directory exists
 *   - checks for emptiness by scanning listing from L 0
 *   - then deletes the directory marker
 */
static void cmd_rmdir(int sock, char *cwd, const char *arg) {
    char path[PATHBUF];
    char fsname[PATHBUF];
    char cmd[MAXLINE];
    char resp[8192];  /* large buffer for directory listing */

    if (arg == NULL || arg[0] == '\0') {
        fprintf(stderr, "rmdir: missing directory name\n");
        return;
    }

    join_path(cwd, arg, path, sizeof(path));
    if (strcmp(path, "/") == 0) {
        fprintf(stderr, "rmdir: cannot remove root directory\n");
        return;
    }

    path_to_fsname(path, fsname, sizeof(fsname));
    if (fsname[0] == '\0') {
        fprintf(stderr, "rmdir: internal error (empty fsname)\n");
        return;
    }

    /* First check that the directory marker exists. */
    snprintf(cmd, sizeof(cmd), "R %s\n", fsname);
    if (fs_simple_cmd(sock, cmd, resp, sizeof(resp)) < 0) {
        perror("rmdir: send/recv");
        exit(1);
    }
    int code = -1;
    if (sscanf(resp, "%d", &code) != 1) {
        fprintf(stderr, "rmdir: unexpected response: %s", resp);
        return;
    }
    if (code == 1) {
        fprintf(stderr, "rmdir: '%s' does not exist\n", path);
        return;
    }
    if (code != 0) {
        fprintf(stderr, "rmdir: error code %d while accessing '%s'\n", code, path);
        return;
    }

    /* Now check emptiness: no other entry name should start with fsname. */
    snprintf(cmd, sizeof(cmd), "L 0\n");
    if (fs_simple_cmd(sock, cmd, resp, sizeof(resp)) < 0) {
        perror("rmdir: send/recv");
        exit(1);
    }

    /* resp now contains listing; each line is a filename. */
    size_t fsname_len = strlen(fsname);
    char *line = strtok(resp, "\n");
    while (line != NULL) {
        if (strncmp(line, fsname, fsname_len) == 0 &&
            strcmp(line, fsname) != 0) {
            fprintf(stderr, "rmdir: directory '%s' is not empty\n", path);
            return;
        }
        line = strtok(NULL, "\n");
    }

    /* Safe to delete the directory marker itself. */
    snprintf(cmd, sizeof(cmd), "D %s\n", fsname);
    if (fs_simple_cmd(sock, cmd, resp, sizeof(resp)) < 0) {
        perror("rmdir: send/recv");
        exit(1);
    }
    code = -1;
    if (sscanf(resp, "%d", &code) != 1) {
        fprintf(stderr, "rmdir: unexpected response: %s", resp);
        return;
    }

    if (code == 0) {
        /* success */
        return;
    } else if (code == 1) {
        fprintf(stderr, "rmdir: '%s' does not exist (race)\n", path);
    } else {
        fprintf(stderr, "rmdir: failed with code %d\n", code);
    }
}

/* ------------------------------------------------------------------ */
/* Show help text. */
static void cmd_help(void) {
    printf("Available commands:\n");
    printf("  mkdir <dirname>   - create a directory\n");
    printf("  cd <dirname>      - change current directory\n");
    printf("  pwd               - print current directory\n");
    printf("  rmdir <dirname>   - remove a directory (must be empty)\n");
    printf("  help              - show this help\n");
    printf("  quit / exit       - exit the program\n");
}

/* ------------------------------------------------------------------ */
int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <fs_server_host> <fs_server_port>\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    if (port <= 0) {
        fprintf(stderr, "Invalid port: %s\n", argv[2]);
        return 1;
    }

    /* Create socket and connect to fs_server. */
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        perror("inet_pton");
        close(s);
        return 1;
    }

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(s);
        return 1;
    }

    /* Current working directory starts at root. */
    char cwd[PATHBUF] = "/";
    char line[MAXLINE];

    printf("Connected to fs_server at %s:%d\n", host, port);
    printf("Type 'help' for a list of commands.\n");

    for (;;) {
        printf("fs:%s$ ", cwd);
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            /* EOF or read error -> exit. */
            putchar('\n');
            break;
        }

        /* Strip leading whitespace. */
        char *p = line;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            p++;
        }
        if (*p == '\0') {
            continue;   /* empty line */
        }

        /* Split into command and optional argument (single word). */
        char cmd[32];
        char arg[PATHBUF];
        cmd[0] = '\0';
        arg[0] = '\0';

        /* We intentionally treat everything after the first space as
         * a single argument without spaces (as per assignment). */
        if (sscanf(p, "%31s %1023s", cmd, arg) < 1) {
            continue;
        }

        if (strcmp(cmd, "mkdir") == 0) {
            cmd_mkdir(s, cwd, (arg[0] ? arg : NULL));
        } else if (strcmp(cmd, "cd") == 0) {
            cmd_cd(s, cwd, (arg[0] ? arg : NULL));
        } else if (strcmp(cmd, "pwd") == 0) {
            cmd_pwd(cwd);
        } else if (strcmp(cmd, "rmdir") == 0) {
            cmd_rmdir(s, cwd, (arg[0] ? arg : NULL));
        } else if (strcmp(cmd, "help") == 0) {
            cmd_help();
        } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            break;
        } else {
            fprintf(stderr, "Unknown command: %s (type 'help')\n", cmd);
        }
    }

    close(s);
    return 0;
}

