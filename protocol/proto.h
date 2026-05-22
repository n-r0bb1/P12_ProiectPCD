/* protocol/proto.h – protocol constants, structs, and function declarations */
#ifndef PROTO_H
#define PROTO_H

#include <stddef.h>

/* ── constants ──────────────────────────────────────────────────────────── */
#define PROTO_REQ_PREFIX      "REQ|"
#define PROTO_RES_PREFIX      "RES|"
#define PROTO_ADM_PREFIX      "ADM|"
#define PROTO_UPLOAD_PREFIX   "UPLOAD|"
#define PROTO_DOWNLOAD_PREFIX "DOWNLOAD|"
#define PROTO_BUF_SIZE        1024
#define PROTO_PATH_MAX        512
#define PROTO_FIELDS_REQ      4   /* REQ|path|chunk|threshold */
#define PROTO_MAX_FILE_SIZE   (500L * 1024L * 1024L)  /* 500 MB */

/* ── Milestone-1 structs ───────────────────────────────────────────────── */
//Parsare req client
typedef struct {
    char video_path[PROTO_PATH_MAX];
    int  chunk_size;
    int  threshold;
} ProtoRequest;

//Raspuns parsar de la server
typedef struct {
    long no_motion_count;
} ProtoResponse;

/* ── Milestone-1 serialize / parse ────────────────────────────────────── */
int proto_serialize_request(const ProtoRequest *req, char *buf, size_t bufsz);
int proto_parse_request(const char *buf, ProtoRequest *out);
int proto_serialize_response(const ProtoResponse *res, char *buf, size_t bufsz);
int proto_parse_response(const char *buf, ProtoResponse *out);

/* ── Milestone-2 I/O helpers ───────────────────────────────────────────── */

/* Sends exactly len bytes; retries on partial sends. 0 = success, -1 = error. */
int send_all(int fd, const void *buf, size_t len);

/* Receives exactly len bytes; retries on partial recvs. 0 = success, -1 = error. */
int recv_all(int fd, void *buf, size_t len);

/* Client-side upload: sends UPLOAD header, waits READY, sends binary, waits
   RES|UPLOAD_OK. Returns 0 on success, -1 on error. */
int send_file(int fd, const char *filepath);

/* Server-side upload: caller has already parsed the UPLOAD header line.
   Sends READY, receives binary into dest_dir/filename.
   Returns 0 on success, -1 on error. */
int recv_file(int fd, const char *filename, long filesize, const char *dest_dir);

/* Server-side download: sends DOWNLOAD header, waits READY, sends binary,
   sends RES|DOWNLOAD_DONE. Returns 0 on success, -1 on error. */
int send_file_to_client(int fd, const char *filepath);

/* Client-side download: reads DOWNLOAD header, sends READY, receives binary,
   saves to cwd. Returns 0 on success, -1 on error. */
int recv_file_from_server(int fd);

#endif /* PROTO_H */
