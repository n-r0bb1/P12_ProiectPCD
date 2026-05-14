#ifndef PROTO_H
#define PROTO_H

#include <stddef.h>

/* Protocol constants */
#define PROTO_REQ_PREFIX    "REQ|"
#define PROTO_RES_PREFIX    "RES|"
#define PROTO_BUF_SIZE      1024
#define PROTO_PATH_MAX      512
#define PROTO_FIELDS_REQ    4   /* REQ|path|chunk|threshold */

/* Parsed request from client */
typedef struct {
    char   video_path[PROTO_PATH_MAX];
    int    chunk_size;
    int    threshold;
} ProtoRequest;

/* Parsed response from server */
typedef struct {
    long no_motion_count;
} ProtoResponse;

/* Serialize/deserialize */
int  proto_serialize_request(const ProtoRequest *req, char *buf, size_t bufsz);
int  proto_parse_request(const char *buf, ProtoRequest *out);
int  proto_serialize_response(const ProtoResponse *res, char *buf, size_t bufsz);
int  proto_parse_response(const char *buf, ProtoResponse *out);

#endif /* PROTO_H */
