/* inet/remoteclient.c – standalone TCP remote client with file transfer */
#include "../protocol/proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define USAGE_MSG \
    "Usage: ./remoteclient -h <host> -p <port> -o <operation> " \
    "[-f <filepath>] [-j <job_id>] [-t <threshold>] [-c <chunk_size>]\n" \
    "  Operations: PROCESS_VIDEO, UPLOAD_VIDEO, DOWNLOAD_RESULT,\n" \
    "              GET_RESULT, GET_PROGRESS, DELETE_RESULT\n"

#define ERRBUF_SIZE  64
#define HOST_MAX     256
#define OP_MAX       32
#define PATH_MAX_RC  PROTO_PATH_MAX
#define RESP_BUF     4096

static void print_usage(void)
{
    (void)fprintf(stderr, USAGE_MSG);
}

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

static int connect_to_server(const char *host, int port)
{
    char portstr[16];
    if (snprintf(portstr, sizeof(portstr), "%d", port) < 0) { return -1; }

    struct addrinfo hints;
    (void)memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || res == NULL) {
        (void)fprintf(stderr,
                      "[remoteclient] cannot resolve host: %s\n", host);
        return -1;
    }

    int sfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sfd == -1) {
        char errbuf[ERRBUF_SIZE];
        (void)strerror_r(errno, errbuf, sizeof(errbuf));
        (void)fprintf(stderr, "[remoteclient] socket: %s\n", errbuf);
        freeaddrinfo(res);
        return -1;
    }

    if (connect(sfd, res->ai_addr, res->ai_addrlen) == -1) {
        char errbuf[ERRBUF_SIZE];
        (void)strerror_r(errno, errbuf, sizeof(errbuf));
        (void)fprintf(stderr, "[remoteclient] connect: %s\n", errbuf);
        freeaddrinfo(res);
        (void)close(sfd);
        return -1;
    }
    freeaddrinfo(res);
    return sfd;
}

static void read_and_print_response(int sfd)
{
    char    buf[RESP_BUF];
    ssize_t nr = 0;
    while ((nr = read(sfd, buf, sizeof(buf) - 1)) > 0) {
        buf[nr] = '\0';
        (void)printf("%s", buf);
    }
}

int main(int argc, char *argv[])
{
    char host[HOST_MAX];
    char op[OP_MAX];
    char filepath[PATH_MAX_RC];
    int  port       = 0;
    int  job_id     = -1;
    int  threshold  = -1;
    int  chunk_size = 0;

    host[0]     = '\0';
    op[0]       = '\0';
    filepath[0] = '\0';

    int opt = 0;
    while ((opt = getopt(argc, argv, "h:p:o:f:j:t:c:")) != -1) { /* NOLINT */
        switch (opt) {

        case 'h':
            if (snprintf(host, sizeof(host),
                         "%s", optarg /* NOLINT(misc-include-cleaner) */) < 0) {
                (void)fprintf(stderr, "[remoteclient] host too long\n");
                return 1;
            }
            break;

        case 'p': {
            char *ep  = NULL;
            long  val = strtol(optarg /* NOLINT */, &ep, 10);
            if (ep == optarg /* NOLINT */ || *ep != '\0'
                    || val <= 0 || val > 65535) {
                (void)fprintf(stderr,
                              "[remoteclient] -p must be 1-65535\n");
                print_usage();
                return 1;
            }
            port = (int)val;
            break;
        }

        case 'o':
            if (snprintf(op, sizeof(op),
                         "%s", optarg /* NOLINT(misc-include-cleaner) */) < 0) {
                (void)fprintf(stderr, "[remoteclient] operation too long\n");
                return 1;
            }
            break;

        case 'f':
            if (snprintf(filepath, sizeof(filepath),
                         "%s", optarg /* NOLINT(misc-include-cleaner) */) < 0) {
                (void)fprintf(stderr, "[remoteclient] filepath too long\n");
                return 1;
            }
            break;

        case 'j': {
            char *ep  = NULL;
            long  val = strtol(optarg /* NOLINT */, &ep, 10);
            if (ep == optarg /* NOLINT */ || *ep != '\0' || val < 0) {
                (void)fprintf(stderr,
                              "[remoteclient] -j must be a non-negative integer\n");
                print_usage();
                return 1;
            }
            job_id = (int)val;
            break;
        }

        case 't': {
            char *ep  = NULL;
            long  val = strtol(optarg /* NOLINT */, &ep, 10);
            if (ep == optarg /* NOLINT */ || *ep != '\0' || val < 0) {
                (void)fprintf(stderr,
                              "[remoteclient] -t must be >= 0\n");
                print_usage();
                return 1;
            }
            threshold = (int)val;
            break;
        }

        case 'c': {
            char *ep  = NULL;
            long  val = strtol(optarg /* NOLINT */, &ep, 10);
            if (ep == optarg /* NOLINT */ || *ep != '\0' || val <= 0) {
                (void)fprintf(stderr,
                              "[remoteclient] -c must be > 0\n");
                print_usage();
                return 1;
            }
            chunk_size = (int)val;
            break;
        }

        default:
            print_usage();
            return 1;
        }
    }

    /* mandatory flags */
    if (host[0] == '\0' || port == 0 || op[0] == '\0') {
        (void)fprintf(stderr,
                      "[remoteclient] -h, -p and -o are mandatory\n");
        print_usage();
        return 1;
    }

    /* per-operation mandatory argument checks */
    if ((strcmp(op, "UPLOAD_VIDEO")   == 0 ||
         strcmp(op, "DOWNLOAD_RESULT") == 0) && filepath[0] == '\0') {
        (void)fprintf(stderr,
                      "[remoteclient] -f is required for %s\n", op);
        print_usage();
        return 1;
    }
    if ((strcmp(op, "GET_PROGRESS")  == 0 ||
         strcmp(op, "GET_RESULT")    == 0 ||
         strcmp(op, "DELETE_RESULT") == 0) && job_id < 0) {
        (void)fprintf(stderr,
                      "[remoteclient] -j is required for %s\n", op);
        print_usage();
        return 1;
    }
    if ((strcmp(op, "UPLOAD_VIDEO")  == 0 ||
         strcmp(op, "PROCESS_VIDEO") == 0)) {
        if (threshold < 0) {
            (void)fprintf(stderr,
                          "[remoteclient] -t is required for %s\n", op);
            print_usage();
            return 1;
        }
        if (chunk_size <= 0) {
            (void)fprintf(stderr,
                          "[remoteclient] -c is required for %s\n", op);
            print_usage();
            return 1;
        }
    }
    if (strcmp(op, "PROCESS_VIDEO") == 0 && filepath[0] == '\0') {
        (void)fprintf(stderr,
                      "[remoteclient] -f is required for PROCESS_VIDEO\n");
        print_usage();
        return 1;
    }

    /* connect */
    int sfd = connect_to_server(host, port);
    if (sfd == -1) { return 1; }

    /* ── PROCESS_VIDEO ── */
    if (strcmp(op, "PROCESS_VIDEO") == 0) {
        char msg[PROTO_BUF_SIZE];
        int  n = snprintf(msg, sizeof(msg),
                          "REQ|PROCESS_VIDEO|%s|%d|%d\n",
                          filepath, chunk_size, threshold);
        if (n < 0 || (size_t)n >= sizeof(msg)) {
            (void)close(sfd); return 1;
        }
        if (send_all(sfd, msg, (size_t)n) != 0) {
            (void)fprintf(stderr, "[remoteclient] send failed\n");
            (void)close(sfd); return 1;
        }
        read_and_print_response(sfd);
        (void)close(sfd);
        return 0;
    }

    /* ── UPLOAD_VIDEO ── */
    if (strcmp(op, "UPLOAD_VIDEO") == 0) {
        if (send_file(sfd, filepath) != 0) {
            (void)fprintf(stderr, "[remoteclient] upload failed\n");
            (void)close(sfd); return 1;
        }

        /* extract basename for the PROCESS_VIDEO request */
        const char *bname = strrchr(filepath, '/');
        bname = bname ? bname + 1 : filepath;

        char msg[PROTO_BUF_SIZE];
        int  n = snprintf(msg, sizeof(msg),
                          "REQ|PROCESS_VIDEO|uploads/%s|%d|%d\n",
                          bname, chunk_size, threshold);
        if (n < 0 || (size_t)n >= sizeof(msg)) {
            (void)close(sfd); return 1;
        }

        /* need a fresh connection for the second request */
        (void)close(sfd);
        sfd = connect_to_server(host, port);
        if (sfd == -1) { return 1; }

        if (send_all(sfd, msg, (size_t)n) != 0) {
            (void)fprintf(stderr, "[remoteclient] send failed\n");
            (void)close(sfd); return 1;
        }
        read_and_print_response(sfd);
        (void)close(sfd);
        return 0;
    }

    /* ── DOWNLOAD_RESULT ── */
    if (strcmp(op, "DOWNLOAD_RESULT") == 0) {
        char msg[PROTO_BUF_SIZE];
        int  n = snprintf(msg, sizeof(msg),
                          "REQ|DOWNLOAD_RESULT|%s\n", filepath);
        if (n < 0 || (size_t)n >= sizeof(msg)) {
            (void)close(sfd); return 1;
        }
        if (send_all(sfd, msg, (size_t)n) != 0) {
            (void)fprintf(stderr, "[remoteclient] send failed\n");
            (void)close(sfd); return 1;
        }
        if (recv_file_from_server(sfd) == 0) {
            (void)printf("[remoteclient] download complete\n");
        } else {
            (void)fprintf(stderr, "[remoteclient] download failed\n");
            (void)close(sfd); return 1;
        }
        (void)close(sfd);
        return 0;
    }

    /* ── GET_RESULT ── */
    if (strcmp(op, "GET_RESULT") == 0) {
        char msg[PROTO_BUF_SIZE];
        int  n = snprintf(msg, sizeof(msg),
                          "REQ|GET_RESULT|%d\n", job_id);
        if (n < 0 || (size_t)n >= sizeof(msg)) {
            (void)close(sfd); return 1;
        }
        if (send_all(sfd, msg, (size_t)n) != 0) {
            (void)fprintf(stderr, "[remoteclient] send failed\n");
            (void)close(sfd); return 1;
        }
        char resp[RESP_BUF];
        if (read_line_fd(sfd, resp, sizeof(resp)) > 0) {
            (void)printf("%s", resp);
        }
        (void)close(sfd);
        return 0;
    }

    /* ── GET_PROGRESS ── */
    if (strcmp(op, "GET_PROGRESS") == 0) {
        char msg[PROTO_BUF_SIZE];
        int  n = snprintf(msg, sizeof(msg),
                          "REQ|GET_PROGRESS|%d\n", job_id);
        if (n < 0 || (size_t)n >= sizeof(msg)) {
            (void)close(sfd); return 1;
        }
        if (send_all(sfd, msg, (size_t)n) != 0) {
            (void)fprintf(stderr, "[remoteclient] send failed\n");
            (void)close(sfd); return 1;
        }
        char resp[RESP_BUF];
        if (read_line_fd(sfd, resp, sizeof(resp)) > 0) {
            (void)printf("%s", resp);
        }
        (void)close(sfd);
        return 0;
    }

    /* ── DELETE_RESULT ── */
    if (strcmp(op, "DELETE_RESULT") == 0) {
        char msg[PROTO_BUF_SIZE];
        int  n = snprintf(msg, sizeof(msg),
                          "REQ|DELETE_RESULT|%d\n", job_id);
        if (n < 0 || (size_t)n >= sizeof(msg)) {
            (void)close(sfd); return 1;
        }
        if (send_all(sfd, msg, (size_t)n) != 0) {
            (void)fprintf(stderr, "[remoteclient] send failed\n");
            (void)close(sfd); return 1;
        }
        char resp[RESP_BUF];
        if (read_line_fd(sfd, resp, sizeof(resp)) > 0) {
            (void)printf("%s", resp);
        }
        (void)close(sfd);
        return 0;
    }

    (void)fprintf(stderr, "[remoteclient] unknown operation: %s\n", op);
    print_usage();
    (void)close(sfd);
    return 1;
}
