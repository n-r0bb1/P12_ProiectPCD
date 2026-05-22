/* server_f/server_logic.c – client dispatch and operation handlers */
#include "../include/server.h"
#include "../include/config.h"
#include "../protocol/proto.h"
#include "../core/video_io.h"
#include "../core/frame_splitter.h"
#include "../core/worker.h"
#include "serverds.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define ERRBUF_SIZE   64
#define RESULTS_DIR   "results"
#define UPLOADS_DIR   "uploads"
#define RESULT_BUFSZ  256

static long s_jobs_done              = 0;
static long s_total_frames_processed = 0;

typedef int ClientFd;

/* ── line reader ─────────────────────────────────────────────────────────── */
static ssize_t read_line(ClientFd sock_fd, char *buf, size_t bufsz)
{
    size_t  pos = 0;
    ssize_t nr  = 0;
    char    ch  = '\0';

    while (pos + 1 < bufsz) {
        nr = read(sock_fd, &ch, 1);
        if (nr == 0) { break; }
        if (nr < 0)  { return -1; }
        buf[pos++] = ch;
        if (ch == '\n') { break; }
    }
    buf[pos] = '\0';
    return (ssize_t)pos;
}

static int send_line(ClientFd fd, const char *msg)
{
    return send_all(fd, msg, strlen(msg));
}

static void ensure_dir(const char *dir)
{
    struct stat st;
    if (stat(dir, &st) != 0) { (void)mkdir(dir, 0755); }
}

static int filename_safe(const char *name)
{
    if (!name || name[0] == '\0')    { return 0; }
    if (strstr(name, "..") != NULL)  { return 0; }
    if (strchr(name, '/') != NULL)   { return 0; }
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════════
   ADM handler
   ══════════════════════════════════════════════════════════════════════════ */
static void handle_admin_client(ClientFd fd, const char *line)
{
    char tmp[SERVER_BUF_SIZE];
    if (snprintf(tmp, sizeof(tmp), "%s", line) < 0) {
        (void)send_line(fd, "ERR|internal\n");
        return;
    }
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '\n') { tmp[--len] = '\0'; }

    const char *cmd = tmp + 4; /* skip "ADM|" */

    if (strcmp(cmd, "STATUS") == 0) {
        (void)send_line(fd, "RES|OK|running\n");

    } else if (strcmp(cmd, "STATS") == 0) {
        char buf[SERVER_BUF_SIZE];
        int  n = snprintf(buf, sizeof(buf), "RES|%ld|%ld\n",
                          s_jobs_done, s_total_frames_processed);
        if (n > 0 && (size_t)n < sizeof(buf)) { (void)send_line(fd, buf); }

    } else if (strcmp(cmd, "LIST_JOBS") == 0) {
        Job *cur = g_job_queue;
        while (cur != NULL) {
            char jline[SERVER_BUF_SIZE];
            int  n = snprintf(jline, sizeof(jline), "JOB|%d|%d|%d\n",
                              cur->job_id, cur->status, cur->progress);
            if (n > 0 && (size_t)n < sizeof(jline)) {
                (void)send_line(fd, jline);
            }
            cur = cur->next;
        }
        (void)send_line(fd, "END\n");

    } else if (strcmp(cmd, "GET_CONFIG") == 0) {
        char buf[SERVER_BUF_SIZE];
        int  n = snprintf(buf, sizeof(buf), "RES|%d|%d|%d\n",
                          g_server_cfg.max_workers,
                          g_server_cfg.chunk_size,
                          g_server_cfg.threshold);
        if (n > 0 && (size_t)n < sizeof(buf)) { (void)send_line(fd, buf); }

    } else if (strcmp(cmd, "SHUTDOWN") == 0) {
        (void)send_line(fd, "RES|OK\n");
        g_shutdown_flag = 1;

    } else {
        (void)send_line(fd, "ERR|unknown_command\n");
    }
}

/* ══════════════════════════════════════════════════════════════════════════
   Video worker (grandchild process)
   ══════════════════════════════════════════════════════════════════════════ */
static void spawn_video_worker(const char *video_path, int chunk_size,
                                int threshold, int max_workers,
                                int job_id, Job *job)
{
    pid_t pid = fork(); //NOLINT
    if (pid < 0) { //NOLINT
        if (job) { job->status = JOB_ERROR; }
        return;
    }

    if (pid == 0) {
        long total = video_get_frame_count(video_path);
        if (total <= 0) { _Exit(1); }

        FrameChunk chunks[SPLITTER_MAX_CHUNKS];
        size_t     n_chunks = 0;
        if (frame_splitter_split(total, chunk_size,
                                 chunks, &n_chunks) != 0) { _Exit(1); }

        long no_motion = workers_run(video_path, chunks, n_chunks,
                                     threshold, max_workers);
        if (no_motion < 0) { _Exit(1); }

        ensure_dir(RESULTS_DIR);
        char outpath[PROTO_BUF_SIZE];
        if (snprintf(outpath, sizeof(outpath),
                     "%s/%d.txt", RESULTS_DIR, job_id) < 0) { _Exit(1); }

        int outfd = open(outpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (outfd == -1) { _Exit(1); }

        char result[RESULT_BUFSZ];
        int  rlen = snprintf(result, sizeof(result), "%ld\n", no_motion);
        if (rlen > 0) { (void)write(outfd, result, (size_t)rlen); }
        (void)close(outfd);
        _Exit(0);
    }

    if (job) { job->status = JOB_RUNNING; job->progress = 0; }

    int status = 0;
    if (waitpid(pid, &status, 0) == pid) {
        if (job) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) { //NOLINT
                job->status   = JOB_DONE;
                job->progress = 100;
                s_jobs_done++;
            } else {
                job->status = JOB_ERROR;
            }
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════
   REQ handler
   ══════════════════════════════════════════════════════════════════════════ */
static void handle_remote_client(ClientFd fd, const char *line,
                                  int max_workers)
{
    char tmp[SERVER_BUF_SIZE];
    if (snprintf(tmp, sizeof(tmp), "%s", line) < 0) {
        (void)send_line(fd, "ERR|internal\n");
        return;
    }
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '\n') { tmp[--len] = '\0'; }

    char *saveptr = NULL;
    char *tok     = strtok_r(tmp, "|", &saveptr); /* "REQ" */
    if (!tok) { (void)send_line(fd, "ERR|malformed\n"); return; }

    char *op = strtok_r(NULL, "|", &saveptr);
    if (!op)  { (void)send_line(fd, "ERR|missing_operation\n"); return; }

    if (strcmp(op, "PROCESS_VIDEO") == 0) {
        char *path = strtok_r(NULL, "|", &saveptr);
        char *schk = strtok_r(NULL, "|", &saveptr);
        char *sthr = strtok_r(NULL, "|", &saveptr);
        if (!path || !schk || !sthr) {
            (void)send_line(fd, "ERR|missing_args\n"); return;
        }
        if (access(path, R_OK) != 0) {
            (void)send_line(fd, "ERR|file_not_found\n"); return;
        }
        char *ep  = NULL;
        long  chk = strtol(schk, &ep, 10);
        if (ep == schk || *ep != '\0' || chk <= 0) {
            (void)send_line(fd, "ERR|invalid_chunk\n"); return;
        }
        ep       = NULL;
        long thr = strtol(sthr, &ep, 10);
        if (ep == sthr || *ep != '\0' || thr < 0) {
            (void)send_line(fd, "ERR|invalid_threshold\n"); return;
        }

        int  jid = next_job_id();
        Job *job = queue_push(&g_job_queue, jid, path,
                              (int)chk, (int)thr);
        spawn_video_worker(path, (int)chk, (int)thr,
                           max_workers, jid, job);
        if (job) { s_total_frames_processed++; }

        char resp[SERVER_BUF_SIZE];
        int  n = snprintf(resp, sizeof(resp), "RES|JOB_ID|%d\n", jid);
        if (n > 0 && (size_t)n < sizeof(resp)) { (void)send_line(fd, resp); }
        return;
    }

    if (strcmp(op, "DOWNLOAD_RESULT") == 0) {
        char *fname = strtok_r(NULL, "|", &saveptr);
        if (!fname || !filename_safe(fname)) {
            (void)send_line(fd, "ERR|invalid_filename\n"); return;
        }
        char fpath[PROTO_BUF_SIZE];
        if (snprintf(fpath, sizeof(fpath),
                     "%s/%s", RESULTS_DIR, fname) < 0) {
            (void)send_line(fd, "ERR|internal\n"); return;
        }
        if (send_file_to_client(fd, fpath) != 0) {
            (void)send_line(fd, "ERR|download_failed\n");
        }
        return;
    }

    if (strcmp(op, "GET_RESULT") == 0) {
        char *sjid = strtok_r(NULL, "|", &saveptr);
        if (!sjid) { (void)send_line(fd, "ERR|missing_job_id\n"); return; }
        char *ep  = NULL;
        long  jid = strtol(sjid, &ep, 10);
        if (ep == sjid || *ep != '\0') {
            (void)send_line(fd, "ERR|invalid_job_id\n"); return;
        }
        char rpath[PROTO_BUF_SIZE];
        if (snprintf(rpath, sizeof(rpath),
                     "%s/%ld.txt", RESULTS_DIR, jid) < 0) {
            (void)send_line(fd, "ERR|internal\n"); return;
        }
        int rfd = open(rpath, O_RDONLY);
        if (rfd == -1) { (void)send_line(fd, "ERR|NOT_FOUND\n"); return; }
        char content[RESULT_BUFSZ];
        ssize_t nr = read(rfd, content, sizeof(content) - 1);
        (void)close(rfd);
        if (nr < 0) { (void)send_line(fd, "ERR|read_error\n"); return; }
        content[nr] = '\0';
        size_t clen = strlen(content);
        if (clen > 0 && content[clen - 1] == '\n') { content[--clen] = '\0'; }
        char resp[SERVER_BUF_SIZE];
        int  n = snprintf(resp, sizeof(resp), "RES|%s\n", content);
        if (n > 0 && (size_t)n < sizeof(resp)) { (void)send_line(fd, resp); }
        return;
    }

    if (strcmp(op, "GET_PROGRESS") == 0) {
        char *sjid = strtok_r(NULL, "|", &saveptr);
        if (!sjid) { (void)send_line(fd, "ERR|missing_job_id\n"); return; }
        char *ep  = NULL;
        long  jid = strtol(sjid, &ep, 10);
        if (ep == sjid || *ep != '\0') {
            (void)send_line(fd, "ERR|invalid_job_id\n"); return;
        }
        Job *job = queue_find(g_job_queue, (int)jid);
        if (!job) { (void)send_line(fd, "ERR|NOT_FOUND\n"); return; }
        char resp[SERVER_BUF_SIZE];
        int  n = snprintf(resp, sizeof(resp),
                          "RES|PROGRESS|%d\n", job->progress);
        if (n > 0 && (size_t)n < sizeof(resp)) { (void)send_line(fd, resp); }
        return;
    }

    if (strcmp(op, "DELETE_RESULT") == 0) {
        char *sjid = strtok_r(NULL, "|", &saveptr);
        if (!sjid) { (void)send_line(fd, "ERR|missing_job_id\n"); return; }
        char *ep  = NULL;
        long  jid = strtol(sjid, &ep, 10);
        if (ep == sjid || *ep != '\0') {
            (void)send_line(fd, "ERR|invalid_job_id\n"); return;
        }
        char rpath[PROTO_BUF_SIZE];
        if (snprintf(rpath, sizeof(rpath),
                     "%s/%ld.txt", RESULTS_DIR, jid) < 0) {
            (void)send_line(fd, "ERR|internal\n"); return;
        }
        if (remove(rpath) == 0) { (void)send_line(fd, "RES|OK\n"); }
        else                    { (void)send_line(fd, "ERR|NOT_FOUND\n"); }
        return;
    }

    (void)send_line(fd, "ERR|unknown_operation\n");
}

/* ══════════════════════════════════════════════════════════════════════════
   UPLOAD handler
   ══════════════════════════════════════════════════════════════════════════ */
static void handle_upload(ClientFd fd, const char *line)
{
    char tmp[SERVER_BUF_SIZE];
    if (snprintf(tmp, sizeof(tmp), "%s", line) < 0) {
        (void)send_line(fd, "ERR|internal\n"); return;
    }
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '\n') { tmp[--len] = '\0'; }

    char *saveptr = NULL;
    (void)strtok_r(tmp, "|", &saveptr); /* "UPLOAD" */

    char *fname = strtok_r(NULL, "|", &saveptr);
    char *sfsz  = strtok_r(NULL, "|", &saveptr);

    if (!fname || !sfsz) {
        (void)send_line(fd, "ERR|malformed\n"); return;
    }
    if (!filename_safe(fname)) {
        (void)send_line(fd, "ERR|invalid_filename\n"); return;
    }
    char *ep       = NULL;
    long  filesize = strtol(sfsz, &ep, 10);
    if (ep == sfsz || *ep != '\0' || filesize < 0
            || filesize > PROTO_MAX_FILE_SIZE) {
        (void)send_line(fd, "ERR|invalid_filesize\n"); return;
    }

    ensure_dir(UPLOADS_DIR);
    if (recv_file(fd, fname, filesize, UPLOADS_DIR) != 0) {
        (void)send_line(fd, "ERR|upload_failed\n");
    }
    /* on success recv_file already sent RES|UPLOAD_OK */
}

/* ══════════════════════════════════════════════════════════════════════════
   Main dispatch (entry point called from serverds.c)
   ══════════════════════════════════════════════════════════════════════════ */
void server_handle_client(ClientFd client_fd, int max_workers)
{
    char req_buf[SERVER_BUF_SIZE];
    ssize_t nr = read_line(client_fd, req_buf, sizeof(req_buf));
    if (nr <= 0) {
        (void)fprintf(stderr, "[server] failed to read request\n");
        return;
    }

    if (strncmp(req_buf, "ADM|", 4) == 0) {
        handle_admin_client(client_fd, req_buf);
        return;
    }

    if (strncmp(req_buf, "UPLOAD|", 7) == 0) {
        handle_upload(client_fd, req_buf);
        return;
    }

    if (strncmp(req_buf, "REQ|", 4) == 0) {
        /* Try Milestone-1 simple format first */
        ProtoRequest req = {"", 0, 0};
        if (proto_parse_request(req_buf, &req) == 0) {
            (void)fprintf(stdout,
                          "[server] REQ video=%s chunk=%d threshold=%d\n",
                          req.video_path, req.chunk_size, req.threshold);

            long total_frames = video_get_frame_count(req.video_path);
            if (total_frames <= 0) {
                (void)fprintf(stderr,
                              "[server] cannot get frame count for %s\n",
                              req.video_path);
                return;
            }
            FrameChunk chunks[SPLITTER_MAX_CHUNKS];
            size_t     n_chunks = 0;
            if (frame_splitter_split(total_frames, req.chunk_size,
                                     chunks, &n_chunks) != 0) {
                (void)fprintf(stderr, "[server] frame split failed\n");
                return;
            }
            long no_motion = workers_run(req.video_path, chunks, n_chunks,
                                         req.threshold, max_workers);
            if (no_motion < 0) {
                (void)fprintf(stderr, "[server] workers failed\n");
                return;
            }
            (void)fprintf(stdout,
                          "[server] no_motion_frames=%ld\n", no_motion);
            char res_buf[SERVER_BUF_SIZE];
            ProtoResponse res = {no_motion};
            if (proto_serialize_response(&res, res_buf,
                                         sizeof(res_buf)) != 0) {
                (void)fprintf(stderr,
                              "[server] serialize response failed\n");
                return;
            }
            size_t  to_send = strlen(res_buf);
            ssize_t sent    = write(client_fd, res_buf, to_send);
            if (sent != (ssize_t)to_send) {
                char errbuf[ERRBUF_SIZE];
                (void)strerror_r(errno, errbuf, sizeof(errbuf));
                (void)fprintf(stderr,
                              "[server] write response failed: %s\n",
                              errbuf);
            }
            return;
        }
        handle_remote_client(client_fd, req_buf, max_workers);
        return;
    }

    (void)send_line(client_fd, "ERR|unknown_request\n");
}