/* protocol/proto.c – protocol serialization, parsing, and file-transfer helpers */
#include "proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>

#define XFER_BUF_SIZE 65536

/* ── internal line reader ─────────────────────────────────────────────────── */

static ssize_t read_line_fd(int fd, char *buf, size_t bufsz)
{
    size_t  pos = 0;
    ssize_t nr  = 0;
    char    ch  = '\0';

    while (pos + 1 < bufsz) {
        nr = read(fd, &ch, 1);
        if (nr == 0) { break; }
        if (nr < 0)  { return -1; }
        buf[pos++] = ch;
        if (ch == '\n') { break; }
    }
    buf[pos] = '\0';
    return (ssize_t)pos;
}

/* ── Milestone-1: serialize / parse ──────────────────────────────────────── */

int proto_serialize_request(const ProtoRequest *req, char *buf, size_t bufsz)
{
    if (!req || !buf || bufsz == 0) { return -1; }

    int ret = snprintf(buf, bufsz, "REQ|%s|%d|%d\n",
                       req->video_path, req->chunk_size, req->threshold);
    if (ret < 0 || (size_t)ret >= bufsz) { return -1; }
    return 0;
}

int proto_parse_request(const char *buf, ProtoRequest *out)
{
    if (!buf || !out) { return -1; }

    char tmp[PROTO_BUF_SIZE];
    if (snprintf(tmp, sizeof(tmp), "%s", buf) < 0) { return -1; }

    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '\n') { tmp[len - 1] = '\0'; }

    char *saveptr = NULL;
    char *tok     = strtok_r(tmp, "|", &saveptr);
    if (!tok || strcmp(tok, "REQ") != 0) { return -1; }

    tok = strtok_r(NULL, "|", &saveptr);
    if (!tok) { return -1; }
    if (snprintf(out->video_path, sizeof(out->video_path), "%s", tok) < 0) { return -1; }

    tok = strtok_r(NULL, "|", &saveptr);
    if (!tok) { return -1; }
    char *endptr = NULL;
    long  chunk  = strtol(tok, &endptr, 10);
    if (endptr == tok || *endptr != '\0' || chunk <= 0) { return -1; }
    out->chunk_size = (int)chunk;

    tok = strtok_r(NULL, "|", &saveptr);
    if (!tok) { return -1; }
    endptr   = NULL;
    long thr = strtol(tok, &endptr, 10);
    if (endptr == tok || *endptr != '\0' || thr < 0) { return -1; }
    out->threshold = (int)thr;

    return 0;
}

int proto_serialize_response(const ProtoResponse *res, char *buf, size_t bufsz)
{
    if (!res || !buf || bufsz == 0) { return -1; }

    int ret = snprintf(buf, bufsz, "RES|%ld\n", res->no_motion_count);
    if (ret < 0 || (size_t)ret >= bufsz) { return -1; }
    return 0;
}

int proto_parse_response(const char *buf, ProtoResponse *out)
{
    if (!buf || !out) { return -1; }

    char tmp[PROTO_BUF_SIZE];
    if (snprintf(tmp, sizeof(tmp), "%s", buf) < 0) { return -1; }

    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '\n') { tmp[len - 1] = '\0'; }

    char *saveptr = NULL;
    char *tok     = strtok_r(tmp, "|", &saveptr);
    if (!tok || strcmp(tok, "RES") != 0) { return -1; }

    tok = strtok_r(NULL, "|", &saveptr);
    if (!tok) { return -1; }
    char *endptr = NULL;
    long  count  = strtol(tok, &endptr, 10);
    if (endptr == tok || *endptr != '\0' || count < 0) { return -1; }
    out->no_motion_count = count;

    return 0;
}

/* ── Milestone-2: I/O helpers ────────────────────────────────────────────── */

int send_all(int fd, const void *buf, size_t len)
{
    const char *ptr  = (const char *)buf;
    size_t      left = len;

    while (left > 0) {
        ssize_t sent = send(fd, ptr, left, 0);
        if (sent < 0) {
            if (errno == EINTR) { continue; }
            return -1;
        }
        ptr  += (size_t)sent;
        left -= (size_t)sent;
    }
    return 0;
}

int recv_all(int fd, void *buf, size_t len)
{
    char   *ptr  = (char *)buf;
    size_t  left = len;

    while (left > 0) {
        ssize_t nr = recv(fd, ptr, left, 0);
        if (nr < 0) {
            if (errno == EINTR) { continue; }
            return -1;
        }
        if (nr == 0) { return -1; }
        ptr  += (size_t)nr;
        left -= (size_t)nr;
    }
    return 0;
}

static const char *path_basename(const char *path)
{
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

int send_file(int fd, const char *filepath)
{
    if (!filepath) { return -1; }

    struct stat st;
    if (stat(filepath, &st) != 0) { return -1; }
    long filesize = (long)st.st_size;
    if (filesize < 0 || filesize > PROTO_MAX_FILE_SIZE) { return -1; }

    const char *bname = path_basename(filepath);
    if (bname[0] == '\0') { return -1; }

    char hdr[PROTO_BUF_SIZE];
    int  hlen = snprintf(hdr, sizeof(hdr), "UPLOAD|%s|%ld\n", bname, filesize);
    if (hlen < 0 || (size_t)hlen >= sizeof(hdr)) { return -1; }
    if (send_all(fd, hdr, (size_t)hlen) != 0) { return -1; }

    char line[PROTO_BUF_SIZE];
    if (read_line_fd(fd, line, sizeof(line)) <= 0) { return -1; }
    if (strncmp(line, "READY", 5) != 0) { return -1; }

    int src = open(filepath, O_RDONLY);
    if (src == -1) { return -1; }

    char xbuf[XFER_BUF_SIZE];
    int  rc = 0;
    while (rc == 0) {
        ssize_t nr = read(src, xbuf, sizeof(xbuf));
        if (nr < 0) { rc = -1; break; }
        if (nr == 0) { break; }
        if (send_all(fd, xbuf, (size_t)nr) != 0) { rc = -1; break; }
    }
    (void)close(src);
    if (rc != 0) { return -1; }

    if (read_line_fd(fd, line, sizeof(line)) <= 0) { return -1; }
    if (strncmp(line, "RES|UPLOAD_OK", 13) != 0) { return -1; }

    return 0;
}

int recv_file(int fd, const char *filename, long filesize, const char *dest_dir)
{
    if (!filename || !dest_dir || filesize < 0 || filesize > PROTO_MAX_FILE_SIZE) {
        return -1;
    }
    if (strstr(filename, "..") != NULL || strchr(filename, '/') != NULL) { return -1; }
    if (filename[0] == '\0') { return -1; }

    if (send_all(fd, "READY\n", 6) != 0) { return -1; }

    char destpath[PROTO_BUF_SIZE];
    int  dlen = snprintf(destpath, sizeof(destpath), "%s/%s", dest_dir, filename);
    if (dlen < 0 || (size_t)dlen >= sizeof(destpath)) { return -1; }

    int outfd = open(destpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outfd == -1) { return -1; }

    char   xbuf[XFER_BUF_SIZE];
    long   remaining = filesize;
    int    rc        = 0;

    while (remaining > 0 && rc == 0) {
        size_t want = (remaining > (long)sizeof(xbuf))
                          ? sizeof(xbuf)
                          : (size_t)remaining;
        if (recv_all(fd, xbuf, want) != 0) { rc = -1; break; }
        ssize_t nw = write(outfd, xbuf, want);
        if (nw != (ssize_t)want) { rc = -1; break; }
        remaining -= (long)want;
    }
    (void)close(outfd);
    if (rc != 0) { return -1; }

    /* recv_file is responsible for the UPLOAD_OK ack (spec step 5) */
    if (send_all(fd, "RES|UPLOAD_OK\n", 14) != 0) { return -1; }

    return 0;
}

int send_file_to_client(int fd, const char *filepath)
{
    if (!filepath) { return -1; }

    struct stat st;
    if (stat(filepath, &st) != 0) { return -1; }
    long filesize = (long)st.st_size;
    if (filesize < 0 || filesize > PROTO_MAX_FILE_SIZE) { return -1; }

    const char *bname = path_basename(filepath);
    if (bname[0] == '\0') { return -1; }

    char hdr[PROTO_BUF_SIZE];
    int  hlen = snprintf(hdr, sizeof(hdr), "DOWNLOAD|%s|%ld\n", bname, filesize);
    if (hlen < 0 || (size_t)hlen >= sizeof(hdr)) { return -1; }
    if (send_all(fd, hdr, (size_t)hlen) != 0) { return -1; }

    char line[PROTO_BUF_SIZE];
    if (read_line_fd(fd, line, sizeof(line)) <= 0) { return -1; }
    if (strncmp(line, "READY", 5) != 0) { return -1; }

    int src = open(filepath, O_RDONLY);
    if (src == -1) { return -1; }

    char xbuf[XFER_BUF_SIZE];
    int  rc = 0;
    while (rc == 0) {
        ssize_t nr = read(src, xbuf, sizeof(xbuf));
        if (nr < 0) { rc = -1; break; }
        if (nr == 0) { break; }
        if (send_all(fd, xbuf, (size_t)nr) != 0) { rc = -1; break; }
    }
    (void)close(src);
    if (rc != 0) { return -1; }

    if (send_all(fd, "RES|DOWNLOAD_DONE\n", 18) != 0) { return -1; }

    return 0;
}

int recv_file_from_server(int fd)
{
    char line[PROTO_BUF_SIZE];
    if (read_line_fd(fd, line, sizeof(line)) <= 0) { return -1; }

    size_t llen = strlen(line);
    if (llen > 0 && line[llen - 1] == '\n') { line[--llen] = '\0'; }

    char *saveptr = NULL;
    char *tok     = strtok_r(line, "|", &saveptr);
    if (!tok || strcmp(tok, "DOWNLOAD") != 0) { return -1; }

    char *fname = strtok_r(NULL, "|", &saveptr);
    if (!fname || fname[0] == '\0') { return -1; }
    if (strstr(fname, "..") != NULL || strchr(fname, '/') != NULL) { return -1; }

    char *sizestr  = strtok_r(NULL, "|", &saveptr);
    if (!sizestr) { return -1; }
    char *endptr   = NULL;
    long  filesize = strtol(sizestr, &endptr, 10);
    if (endptr == sizestr || *endptr != '\0' || filesize < 0
            || filesize > PROTO_MAX_FILE_SIZE) {
        return -1;
    }

    /* save fname before strtok_r corrupts it on the next call */
    char safe_fname[PROTO_PATH_MAX];
    if (snprintf(safe_fname, sizeof(safe_fname), "%s", fname) < 0) { return -1; }

    if (send_all(fd, "READY\n", 6) != 0) { return -1; }

    int outfd = open(safe_fname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outfd == -1) { return -1; }

    char   xbuf[XFER_BUF_SIZE];
    long   remaining = filesize;
    int    rc        = 0;

    while (remaining > 0 && rc == 0) {
        size_t want = (remaining > (long)sizeof(xbuf))
                          ? sizeof(xbuf)
                          : (size_t)remaining;
        if (recv_all(fd, xbuf, want) != 0) { rc = -1; break; }
        ssize_t nw = write(outfd, xbuf, want);
        if (nw != (ssize_t)want) { rc = -1; break; }
        remaining -= (long)want;
    }
    (void)close(outfd);
    if (rc != 0) { return -1; }

    if (read_line_fd(fd, line, sizeof(line)) <= 0) { return -1; }
    if (strncmp(line, "RES|DOWNLOAD_DONE", 17) != 0) { return -1; }

    return 0;
}
