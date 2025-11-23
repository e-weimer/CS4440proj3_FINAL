// problem4/fs_server.c
// Filesystem server for Project 3 (Problem 4).
// Speaks the FS protocol to clients and uses the disk server protocol underneath.
// FS protocol per handout: F, C f, D f, L b, R f, W f l data.  (flat directory) [spec].
//
// Build/run example:
//   ./fs_server <listen_port> <disk_host> <disk_port>
//
// Example:
//   ./fs_server 5555 127.0.0.1 4443
//
// Then, from another terminal, use fs_cli to send commands.
//
// Notes:
//  - Disk block size = 128 bytes, one sector per block (from Problem 3).
//  - We implement a simple on-disk layout with a superblock, FAT, and fixed-size directory.
//  - FAT entry: 32-bit little-endian. 0 = FREE, 0xFFFFFFFF = EOF, 0xFFFFFFFE = RESERVED/META.
//  - Directory entry: fixed 64 bytes (name[32], length[4], first[4], used[1], pad[23]); two per sector.
//  - "F" formats the disk (writes metadata tables). Other ops require a formatted disk.
//
// Spec refs: FS server commands & responses; flat filesystem design with FAT & directory table. [PDF]
//   - Commands list & return codes. (F, C, D, L, R, W). 
//   - Suggested FAT + directory approach in Appendix.
//
// (c) CS4440 Project 3
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define BLKSZ 128
#define MAX_LINE 4096
#define MAX_NAME 32

// === Disk protocol helpers ===
// We open a *separate* disk connection per client thread for simplicity.
typedef struct {
    int fd;
    long cyl, sec;
} disk_t;

// robust I/O
static ssize_t write_all(int fd, const void* buf, size_t n) {
    size_t off = 0; while (off < n) {
        ssize_t w = send(fd, (const char*)buf + off, n - off, 0);
        if (w < 0) { if (errno == EINTR) continue; return -1; } off += (size_t)w;
    } return (ssize_t)off;
}
static ssize_t read_exact(int fd, void* buf, size_t n) {
    size_t off = 0; while (off < n) {
        ssize_t r = recv(fd, (char*)buf + off, n - off, 0);
        if (r == 0) return off; if (r < 0) { if (errno == EINTR) continue; return -1; } off += (size_t)r;
    } return (ssize_t)off;
}
static ssize_t readline(int fd, char* out) {
    size_t off = 0; while (off + 1 < MAX_LINE) {
        char c; ssize_t r = recv(fd, &c, 1, 0);
        if (r == 0) break; if (r < 0) { if (errno == EINTR) continue; return -1; } out[off++] = c; if (c == '\n') break;
    }
    out[off] = 0; return (ssize_t)off;
}

static int disk_connect(disk_t* d, const char* host, int port) {
    d->fd = socket(AF_INET, SOCK_STREAM, 0); if (d->fd < 0) { perror("socket"); return -1; }
    struct sockaddr_in a = { 0 }; a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1) { perror("inet_pton"); close(d->fd); return -1; }
    if (connect(d->fd, (struct sockaddr*)&a, sizeof(a)) < 0) { perror("connect disk"); close(d->fd); return -1; }
    // Query geometry with I
    const char* I = "I\n"; if (write_all(d->fd, I, 2) < 0) { perror("send I"); close(d->fd); return -1; }
    char buf[64]; int n = recv(d->fd, buf, sizeof(buf) - 1, 0); if (n <= 0) { fprintf(stderr, "disk: no geom\n"); close(d->fd); return -1; }
    buf[n] = 0; if (sscanf(buf, "%ld %ld", &d->cyl, &d->sec) != 2) { fprintf(stderr, "disk: bad geom %s\n", buf); close(d->fd); return -1; }
    return 0;
}
static void disk_close(disk_t* d) { if (d->fd >= 0) close(d->fd); d->fd = -1; }

static inline uint32_t total_blocks(const disk_t* d) { return (uint32_t)(d->cyl * d->sec); }
static inline void idx_to_cs(const disk_t* d, uint32_t idx, long* c, long* s) {
    *c = idx / (uint32_t)d->sec; *s = idx % (uint32_t)d->sec;
}

// Read sector by absolute index
static int disk_read_idx(disk_t* d, uint32_t idx, unsigned char out[BLKSZ]) {
    long c, s; idx_to_cs(d, idx, &c, &s);
    char hdr[64]; int m = snprintf(hdr, sizeof(hdr), "R %ld %ld\n", c, s);
    if (write_all(d->fd, hdr, (size_t)m) < 0) return -1;
    char code; if (read_exact(d->fd, &code, 1) != 1) return -1;
    if (code != '1') return -1;
    return (read_exact(d->fd, out, BLKSZ) == BLKSZ) ? 0 : -1;
}
// Write sector by absolute index. We always write 128 bytes.
static int disk_write_idx(disk_t* d, uint32_t idx, const unsigned char in[BLKSZ]) {
    long c, s; idx_to_cs(d, idx, &c, &s);
    char hdr[64]; int m = snprintf(hdr, sizeof(hdr), "W %ld %ld %d\n", c, s, BLKSZ);
    if (write_all(d->fd, hdr, (size_t)m) < 0) return -1;
    if (write_all(d->fd, in, BLKSZ) < 0) return -1;
    char code; return (read_exact(d->fd, &code, 1) == 1 && code == '1') ? 0 : -1;
}

// === On-disk layout ===
static const uint32_t FAT_FREE = 0x00000000u;
static const uint32_t FAT_EOF = 0xffffffffu;
static const uint32_t FAT_RESERVED = 0xfffffffEu;

typedef struct {
    // cached from superblock
    uint32_t total_blocks;
    uint32_t fat_start;    // sector index
    uint32_t fat_sectors;  // number of sectors used by FAT
    uint32_t dir_start;    // sector index
    uint32_t dir_sectors;  // number of sectors used by directory
    uint32_t dir_entries;  // total directory entries
} layout_t;

// directory entry (64 bytes) — manual packing to avoid padding
typedef struct {
    char     name[MAX_NAME]; // 32
    uint32_t length;         // bytes
    uint32_t first;          // first data block index, or FAT_EOF if empty
    uint8_t  used;           // 0/1
    uint8_t  pad[23];        // 64 - (32+4+4+1) = 23
} dirent_fs;

static void dirent_pack(const dirent_fs* e, unsigned char* dst) {
    memset(dst, 0, 64);
    memcpy(dst, e->name, MAX_NAME);
    memcpy(dst + 32, &e->length, 4);
    memcpy(dst + 36, &e->first, 4);
    dst[40] = e->used;
}
static void dirent_unpack(dirent_fs* e, const unsigned char* src) {
    memset(e, 0, sizeof(*e));
    memcpy(e->name, src, MAX_NAME); e->name[MAX_NAME - 1] = '\0';
    memcpy(&e->length, src + 32, 4);
    memcpy(&e->first, src + 36, 4);
    e->used = src[40];
}

// superblock (sector 0): ASCII tag + fields; manual pack to 128B
static void super_pack(unsigned char* blk, const disk_t* d, const layout_t* L) {
    memset(blk, 0, BLKSZ);
    const char* magic = "CSFS1";
    memcpy(blk, magic, strlen(magic));            // 0..4
    memcpy(blk + 16, &d->cyl, 8);                   // store cyl (long) and sec (long) for info
    memcpy(blk + 24, &d->sec, 8);
    memcpy(blk + 40, &L->total_blocks, 4);
    memcpy(blk + 44, &L->fat_start, 4);
    memcpy(blk + 48, &L->fat_sectors, 4);
    memcpy(blk + 52, &L->dir_start, 4);
    memcpy(blk + 56, &L->dir_sectors, 4);
    memcpy(blk + 60, &L->dir_entries, 4);
}
static int super_load(const unsigned char* blk, disk_t* d, layout_t* L) {
    if (memcmp(blk, "CSFS1", 5) != 0) return -1;
    memcpy(&d->cyl, blk + 16, 8);
    memcpy(&d->sec, blk + 24, 8);
    memcpy(&L->total_blocks, blk + 40, 4);
    memcpy(&L->fat_start, blk + 44, 4);
    memcpy(&L->fat_sectors, blk + 48, 4);
    memcpy(&L->dir_start, blk + 52, 4);
    memcpy(&L->dir_sectors, blk + 56, 4);
    memcpy(&L->dir_entries, blk + 60, 4);
    return 0;
}

// === FAT cache ===
// We'll load/save the whole FAT into memory (uint32_t per block).
typedef struct {
    uint32_t* v;   // size = total_blocks
    bool loaded;
    pthread_mutex_t mtx;
} fat_cache_t;

static void fat_init(fat_cache_t* fc) { fc->v = NULL; fc->loaded = false; pthread_mutex_init(&fc->mtx, NULL); }
static void fat_free(fat_cache_t* fc) { free(fc->v); fc->v = NULL; fc->loaded = false; }

static int fat_load(disk_t* d, const layout_t* L, fat_cache_t* fc) {
    pthread_mutex_lock(&fc->mtx);
    if (fc->loaded) { pthread_mutex_unlock(&fc->mtx); return 0; }
    fc->v = (uint32_t*)calloc(L->total_blocks, sizeof(uint32_t));
    if (!fc->v) { pthread_mutex_unlock(&fc->mtx); return -1; }
    unsigned char blk[BLKSZ];
    uint32_t entries_per_sector = BLKSZ / 4; // 32
    uint32_t idx = 0;
    for (uint32_t s = 0; s < L->fat_sectors; s++) {
        if (disk_read_idx(d, L->fat_start + s, blk) < 0) { pthread_mutex_unlock(&fc->mtx); return -1; }
        for (uint32_t i = 0; i < entries_per_sector && idx < L->total_blocks; i++, idx++) {
            memcpy(&fc->v[idx], blk + i * 4, 4);
        }
    }
    fc->loaded = true;
    pthread_mutex_unlock(&fc->mtx);
    return 0;
}
static int fat_flush(disk_t* d, const layout_t* L, fat_cache_t* fc) {
    pthread_mutex_lock(&fc->mtx);
    if (!fc->loaded) { pthread_mutex_unlock(&fc->mtx); return 0; }
    unsigned char blk[BLKSZ];
    uint32_t entries_per_sector = BLKSZ / 4;
    uint32_t idx = 0;
    for (uint32_t s = 0; s < L->fat_sectors; s++) {
        memset(blk, 0, BLKSZ);
        for (uint32_t i = 0; i < entries_per_sector && idx < L->total_blocks; i++, idx++) {
            memcpy(blk + i * 4, &fc->v[idx], 4);
        }
        if (disk_write_idx(d, L->fat_start + s, blk) < 0) { pthread_mutex_unlock(&fc->mtx); return -1; }
    }
    pthread_mutex_unlock(&fc->mtx);
    return 0;
}
static uint32_t fat_get(fat_cache_t* fc, uint32_t i) { return fc->v[i]; }
static void     fat_set(fat_cache_t* fc, uint32_t i, uint32_t v) { fc->v[i] = v; }

// === Directory helpers ===
static int dir_read_entry(disk_t* d, const layout_t* L, uint32_t slot, dirent_fs* out) {
    uint32_t per_sector = BLKSZ / 64; // 2
    uint32_t sec = L->dir_start + (slot / per_sector);
    uint32_t offs = (slot % per_sector) * 64;
    unsigned char blk[BLKSZ]; if (disk_read_idx(d, sec, blk) < 0) return -1;
    dirent_unpack(out, blk + offs);
    return 0;
}
static int dir_write_entry(disk_t* d, const layout_t* L, uint32_t slot, const dirent_fs* in) {
    uint32_t per_sector = BLKSZ / 64;
    uint32_t sec = L->dir_start + (slot / per_sector);
    uint32_t offs = (slot % per_sector) * 64;
    unsigned char blk[BLKSZ]; if (disk_read_idx(d, sec, blk) < 0) return -1;
    dirent_pack(in, blk + offs);
    if (disk_write_idx(d, sec, blk) < 0) return -1;
    return 0;
}
static int dir_find_by_name(disk_t* d, const layout_t* L, const char* name, uint32_t* slot, dirent_fs* ent) {
    for (uint32_t i = 0; i < L->dir_entries; i++) {
        if (dir_read_entry(d, L, i, ent) < 0) return -1;
        if (ent->used && strncmp(ent->name, name, MAX_NAME) == 0) { *slot = i; return 0; }
    }
    return 1; // not found
}
static int dir_find_free(disk_t* d, const layout_t* L, uint32_t* slot) {
    dirent_fs e;
    for (uint32_t i = 0; i < L->dir_entries; i++) {
        if (dir_read_entry(d, L, i, &e) < 0) return -1;
        if (!e.used) { *slot = i; return 0; }
    }
    return 1; // none
}

// === Allocation ===
static int find_free_block(const layout_t* L, fat_cache_t* fc, uint32_t start_idx, uint32_t* out) {
    for (uint32_t i = start_idx; i < L->total_blocks; i++) {
        if (fc->v[i] == FAT_FREE) { *out = i; return 0; }
    }
    return 1;
}
static int free_chain(disk_t* d, const layout_t* L, fat_cache_t* fc, uint32_t head) {
    (void)d; (void)L; // local only; flushed at end
    uint32_t cur = head;
    while (cur != FAT_EOF) {
        uint32_t nxt = fc->v[cur];
        fc->v[cur] = FAT_FREE;
        if (nxt == FAT_EOF) break;
        cur = nxt;
    }
    return 0;
}

// === Formatting ===
static int compute_layout(const disk_t* disk, layout_t* L) {
    L->total_blocks = total_blocks(disk);
    // FAT: 4 bytes per entry. entries_per_sector = 128/4 = 32
    uint32_t fat_bytes = L->total_blocks * 4;
    uint32_t fat_secs = (fat_bytes + BLKSZ - 1) / BLKSZ;
    // Directory: choose 64 entries (2 per sector -> 32 sectors)
    L->dir_entries = 64;
    L->dir_sectors = 32;

    L->fat_start = 1;                 // superblock at sector 0
    L->fat_sectors = fat_secs;
    L->dir_start = L->fat_start + L->fat_sectors;
    L->dir_sectors = L->dir_sectors;
    return 0;
}
static int format_fs(disk_t* d, layout_t* L, fat_cache_t* fc) {
    if (compute_layout(d, L) < 0) return -1;

    // write superblock
    unsigned char blk[BLKSZ]; super_pack(blk, d, L);
    if (disk_write_idx(d, 0, blk) < 0) return -1;

    // init FAT on disk: mark everything FREE, then mark [0 .. meta_end] as RESERVED
    unsigned char z[BLKSZ]; memset(z, 0, BLKSZ);
    for (uint32_t s = 0; s < L->fat_sectors; s++) {
        if (disk_write_idx(d, L->fat_start + s, z) < 0) return -1;
    }
    // load to cache, then set reservations and flush
    if (fat_load(d, L, fc) < 0) return -1;

    uint32_t meta_end = L->dir_start + L->dir_sectors - 1;
    for (uint32_t i = 0; i <= meta_end && i < L->total_blocks; i++) {
        fc->v[i] = FAT_RESERVED;
    }
    if (fat_flush(d, L, fc) < 0) return -1;

    // clear directory sectors
    memset(z, 0, BLKSZ);
    for (uint32_t s = 0; s < L->dir_sectors; s++) {
        if (disk_write_idx(d, L->dir_start + s, z) < 0) return -1;
    }
    return 0;
}

// === Reading/Writing files ===
static int read_whole_file(disk_t* d, const layout_t* L, fat_cache_t* fc, const dirent_fs* ent, unsigned char** out, uint32_t* outlen) {
    *outlen = ent->length;
    *out = (unsigned char*)malloc(ent->length ? ent->length : 1);
    if (!*out) return -1;
    uint32_t left = ent->length, pos = 0;
    uint32_t cur = ent->first;
    unsigned char blk[BLKSZ];
    while (left > 0 && cur != FAT_EOF) {
        if (disk_read_idx(d, cur, blk) < 0) { free(*out); return -1; }
        uint32_t take = left < BLKSZ ? left : BLKSZ;
        memcpy(*out + pos, blk, take);
        pos += take; left -= take;
        cur = fat_get(fc, cur);
    }
    return 0;
}

static int write_whole_file(disk_t* d, const layout_t* L, fat_cache_t* fc, dirent_fs* ent, const unsigned char* data, uint32_t len) {
    // Free current chain if any
    if (ent->used && ent->first != FAT_EOF) {
        if (free_chain(d, L, fc, ent->first) < 0) return -1;
    }
    ent->first = FAT_EOF;
    ent->length = len;

    if (len == 0) { return 0; }

    // Allocate needed blocks
    uint32_t blocks = (len + BLKSZ - 1) / BLKSZ;
    uint32_t data_start = L->dir_start + L->dir_sectors; // first usable data block
    uint32_t head = FAT_EOF, prev = FAT_EOF;

    for (uint32_t k = 0; k < blocks; k++) {
        uint32_t b;
        if (find_free_block(L, fc, data_start, &b) != 0) return -2; // no space
        fat_set(fc, b, FAT_EOF);
        if (prev != FAT_EOF) fat_set(fc, prev, b);
        else head = b;
        prev = b;
    }

    // Write blocks
    ent->first = head;
    uint32_t cur = head;
    uint32_t left = len, pos = 0;
    unsigned char blk[BLKSZ];
    while (cur != FAT_EOF) {
        memset(blk, 0, BLKSZ);
        uint32_t take = left < BLKSZ ? left : BLKSZ;
        memcpy(blk, data + pos, take);
        if (disk_write_idx(d, cur, blk) < 0) return -1;
        pos += take; if (left > take) left -= take; else left = 0;
        uint32_t nxt = fat_get(fc, cur);
        cur = (nxt == FAT_EOF || pos >= len) ? FAT_EOF : nxt;
    }
    return 0;
}

// === Server state per process ===
typedef struct {
    char disk_host[64];
    int  disk_port;
    layout_t L;
    fat_cache_t fat;
    bool formatted; // superblock present
    pthread_mutex_t meta_mtx; // protects FAT & directory updates
} server_state_t;

static server_state_t G;

typedef struct { int cfd; } client_arg_t;

// Utility: load superblock if present
static int try_load_super(disk_t* d, layout_t* L) {
    unsigned char blk[BLKSZ];
    if (disk_read_idx(d, 0, blk) < 0) return -1;
    return super_load(blk, d, L);
}

// === FS command handlers ===
static int cmd_format(disk_t* disk, int cfd) {
    // (Re-)compute layout then format
    if (compute_layout(disk, &G.L) < 0) return -1;
    pthread_mutex_lock(&G.meta_mtx);
    fat_free(&G.fat); fat_init(&G.fat); // reset cache
    int rv = format_fs(disk, &G.L, &G.fat);
    if (rv == 0) { G.formatted = true; }
    pthread_mutex_unlock(&G.meta_mtx);
    const char* ok = (rv == 0) ? "0\n" : "2\n";
    return (write_all(cfd, ok, strlen(ok)) < 0) ? -1 : 0;
}
static int cmd_create(disk_t* disk, const char* name, int cfd) {
    if (strlen(name) == 0 || strlen(name) >= MAX_NAME) { write_all(cfd, "2\n", 2); return 0; }
    pthread_mutex_lock(&G.meta_mtx);
    if (!G.formatted) { pthread_mutex_unlock(&G.meta_mtx); write_all(cfd, "2\n", 2); return 0; }
    if (fat_load(disk, &G.L, &G.fat) < 0) { pthread_mutex_unlock(&G.meta_mtx); write_all(cfd, "2\n", 2); return 0; }

    dirent_fs e; uint32_t slot;
    int fnd = dir_find_by_name(disk, &G.L, name, &slot, &e);
    if (fnd == 0) { pthread_mutex_unlock(&G.meta_mtx); write_all(cfd, "1\n", 2); return 0; } // already exists

    uint32_t free_slot;
    if (dir_find_free(disk, &G.L, &free_slot) != 0) { pthread_mutex_unlock(&G.meta_mtx); write_all(cfd, "2\n", 2); return 0; }
    memset(&e, 0, sizeof(e)); strncpy(e.name, name, MAX_NAME - 1);
    e.length = 0; e.first = FAT_EOF; e.used = 1;
    int rv = dir_write_entry(disk, &G.L, free_slot, &e);
    pthread_mutex_unlock(&G.meta_mtx);
    write_all(cfd, (rv == 0) ? "0\n" : "2\n", 2);
    return 0;
}
static int cmd_delete(disk_t* disk, const char* name, int cfd) {
    pthread_mutex_lock(&G.meta_mtx);
    if (!G.formatted) { pthread_mutex_unlock(&G.meta_mtx); write_all(cfd, "2\n", 2); return 0; }
    if (fat_load(disk, &G.L, &G.fat) < 0) { pthread_mutex_unlock(&G.meta_mtx); write_all(cfd, "2\n", 2); return 0; }

    dirent_fs e; uint32_t slot;
    if (dir_find_by_name(disk, &G.L, name, &slot, &e) != 0) { pthread_mutex_unlock(&G.meta_mtx); write_all(cfd, "1\n", 2); return 0; }
    // free chain
    if (e.first != FAT_EOF) free_chain(disk, &G.L, &G.fat, e.first);
    if (fat_flush(disk, &G.L, &G.fat) < 0) { pthread_mutex_unlock(&G.meta_mtx); write_all(cfd, "2\n", 2); return 0; }
    // clear dir slot
    dirent_fs blank; memset(&blank, 0, sizeof(blank));
    int rv = dir_write_entry(disk, &G.L, slot, &blank);
    pthread_mutex_unlock(&G.meta_mtx);
    write_all(cfd, (rv == 0) ? "0\n" : "2\n", 2);
    return 0;
}
static int cmd_list(disk_t* disk, int brief, int cfd) {
    if (!G.formatted) { write_all(cfd, "(unformatted)\n", 14); return 0; }
    dirent_fs e;
    char line[256];
    for (uint32_t i = 0; i < G.L.dir_entries; i++) {
        if (dir_read_entry(disk, &G.L, i, &e) < 0) return -1;
        if (!e.used) continue;
        if (!brief) snprintf(line, sizeof(line), "%s %u\n", e.name, e.length);
        else       snprintf(line, sizeof(line), "%s\n", e.name);
        if (write_all(cfd, line, strlen(line)) < 0) return -1;
    }
    return 0;
}
static int cmd_read(disk_t* disk, const char* name, int cfd) {
    pthread_mutex_lock(&G.meta_mtx);
    if (!G.formatted) { pthread_mutex_unlock(&G.meta_mtx); write_all(cfd, "1 0 \n", 5); return 0; }
    if (fat_load(disk, &G.L, &G.fat) < 0) { pthread_mutex_unlock(&G.meta_mtx); write_all(cfd, "2 0 \n", 5); return 0; }
    dirent_fs e; uint32_t slot;
    if (dir_find_by_name(disk, &G.L, name, &slot, &e) != 0) { pthread_mutex_unlock(&G.meta_mtx); write_all(cfd, "1 0 \n", 5); return 0; }
    unsigned char* buf = NULL; uint32_t len = 0;
    int rv = read_whole_file(disk, &G.L, &G.fat, &e, &buf, &len);
    pthread_mutex_unlock(&G.meta_mtx);
    if (rv < 0) { write_all(cfd, "2 0 \n", 5); return 0; }

    char hdr[64]; int m = snprintf(hdr, sizeof(hdr), "0 %u ", len);
    if (write_all(cfd, hdr, (size_t)m) < 0) { free(buf); return -1; }
    if (len > 0 && write_all(cfd, buf, len) < 0) { free(buf); return -1; }
    if (write_all(cfd, "\n", 1) < 0) { free(buf); return -1; }
    free(buf);
    return 0;
}
static int cmd_write(disk_t* disk, const char* name, uint32_t len, int cfd) {
    // read len raw bytes from client, then write file
    unsigned char* data = (unsigned char*)malloc(len ? len : 1); if (!data) { write_all(cfd, "2\n", 2); return 0; }
    if (len > 0 && read_exact(cfd, data, len) != (ssize_t)len) { free(data); return -1; }

    pthread_mutex_lock(&G.meta_mtx);
    if (!G.formatted) { pthread_mutex_unlock(&G.meta_mtx); free(data); write_all(cfd, "2\n", 2); return 0; }
    if (fat_load(disk, &G.L, &G.fat) < 0) { pthread_mutex_unlock(&G.meta_mtx); free(data); write_all(cfd, "2\n", 2); return 0; }
    dirent_fs e; uint32_t slot;
    if (dir_find_by_name(disk, &G.L, name, &slot, &e) != 0) { pthread_mutex_unlock(&G.meta_mtx); free(data); write_all(cfd, "1\n", 2); return 0; }

    int rv = write_whole_file(disk, &G.L, &G.fat, &e, data, len);
    if (rv == -2) { pthread_mutex_unlock(&G.meta_mtx); free(data); write_all(cfd, "2\n", 2); return 0; } // no space
    if (rv < 0) { pthread_mutex_unlock(&G.meta_mtx); free(data); write_all(cfd, "2\n", 2); return 0; }
    // persist updated FAT and dir entry
    if (fat_flush(disk, &G.L, &G.fat) < 0) { pthread_mutex_unlock(&G.meta_mtx); free(data); write_all(cfd, "2\n", 2); return 0; }
    if (dir_write_entry(disk, &G.L, slot, &e) < 0) { pthread_mutex_unlock(&G.meta_mtx); free(data); write_all(cfd, "2\n", 2); return 0; }
    pthread_mutex_unlock(&G.meta_mtx);
    free(data);
    write_all(cfd, "0\n", 2);
    return 0;
}

// === Client handler ===
static void* client_main(void* vp) {
    int cfd = ((client_arg_t*)vp)->cfd; free(vp);
    // open a dedicated disk connection for this client
    disk_t d = { .fd = -1 }; if (disk_connect(&d, G.disk_host, G.disk_port) < 0) { close(cfd); return NULL; }
    // detect existing FS
    layout_t tmpL;
    if (try_load_super(&d, &tmpL) == 0) { G.L = tmpL; G.formatted = true; } // lazy adoption

    char line[MAX_LINE];
    while (1) {
        ssize_t r = readline(cfd, line);
        if (r <= 0) break;
        char cmd = 0; char arg1[MAX_LINE] = { 0 }; char arg2[MAX_LINE] = { 0 };
        uint32_t Lval = 0;
        if (sscanf(line, " %c %s %u", &cmd, arg1, &Lval) < 1) break;

        switch (cmd) {
        case 'F': { cmd_format(&d, cfd); break; } // returns "0\n" or "2\n"
        case 'C': { cmd_create(&d, arg1, cfd); break; } // "0\n" | "1\n" | "2\n"
        case 'D': { cmd_delete(&d, arg1, cfd); break; } // "0\n" | "1\n" | "2\n"
        case 'L': {
            int brief = (arg1[0] == '0') ? 1 : 0; // '0' -> names only
            cmd_list(&d, brief, cfd); break;
        }
        case 'R': { cmd_read(&d, arg1, cfd); break; } // "code len data"
        case 'W': {
            // line was "W f l\n" then raw l bytes
            uint32_t l = 0; char fname[MAX_LINE] = { 0 };
            if (sscanf(line, " W %s %u", fname, &l) != 2) { write_all(cfd, "2\n", 2); break; }
            cmd_write(&d, fname, l, cfd); break;
        }
        default: { /*unknown*/ goto out; }
        }
    }
out:
    disk_close(&d); close(cfd); return NULL;
}

// === Main: listen for FS clients ===
int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <listen_port> <disk_host> <disk_port>\n", argv[0]);
        return 2;
    }
    int lport = atoi(argv[1]);
    strncpy(G.disk_host, argv[2], sizeof(G.disk_host) - 1);
    G.disk_port = atoi(argv[3]);
    pthread_mutex_init(&G.meta_mtx, NULL);
    fat_init(&G.fat);
    G.formatted = false;

    int srv = socket(AF_INET, SOCK_STREAM, 0); if (srv < 0) { perror("socket"); return 1; }
    int opt = 1; setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in a = { 0 }; a.sin_family = AF_INET; a.sin_port = htons((uint16_t)lport); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(srv, (struct sockaddr*)&a, sizeof(a)) < 0) { perror("bind"); return 1; }
    if (listen(srv, 64) < 0) { perror("listen"); return 1; }
    fprintf(stderr, "[fs_server] listening on %d; disk=%s:%d\n", lport, G.disk_host, G.disk_port);

    while (1) {
        struct sockaddr_in cli; socklen_t cl = sizeof(cli);
        int cfd = accept(srv, (struct sockaddr*)&cli, &cl);
        if (cfd < 0) { if (errno == EINTR) continue; perror("accept"); break; }
        client_arg_t* arg = (client_arg_t*)malloc(sizeof(*arg)); if (!arg) { close(cfd); continue; }
        arg->cfd = cfd;
        pthread_t tid; if (pthread_create(&tid, NULL, client_main, arg) == 0) pthread_detach(tid);
        else { perror("pthread_create"); close(cfd); free(arg); }
    }
    close(srv);
    return 0;
}
