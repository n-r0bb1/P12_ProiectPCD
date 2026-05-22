/* inet/adminclient.c – standalone TCP admin client */
#include "../protocol/proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define USAGE_MSG \
    "Usage: ./adminclient -h <host> -p <port> -c <command>\n" \
    "  Commands: STATUS, STATS, LIST_JOBS, SHUTDOWN, GET_CONFIG\n"
#define ERRBUF_SIZE  64
#define HOST_MAX     256
#define CMD_MAX      32
#define RESP_BUF     4096

static void print_usage(void)
{
    (void)fprintf(stderr, USAGE_MSG);
}

static int is_valid_command(const char *cmd)
{
    return (strcmp(cmd, "STATUS")     == 0 ||
            strcmp(cmd, "STATS")      == 0 ||
            strcmp(cmd, "LIST_JOBS")  == 0 ||
            strcmp(cmd, "SHUTDOWN")   == 0 ||
            strcmp(cmd, "GET_CONFIG") == 0);
}

int main(int argc, char *argv[])
{
    char host[HOST_MAX];
    char cmd[CMD_MAX];
    int  port = 0;

    host[0] = '\0';
    cmd[0]  = '\0';

    int opt = 0;
    while ((opt = getopt(argc, argv, "h:p:c:")) != -1) { /* NOLINT */
        switch (opt) {

        case 'h':
            if (snprintf(host, sizeof(host),
                         "%s", optarg /* NOLINT(misc-include-cleaner) */) < 0) {
                (void)fprintf(stderr, "[adminclient] host too long\n");
                return 1;
            }
            break;

        case 'p': {
            char *ep  = NULL;
            long  val = strtol(optarg /* NOLINT */, &ep, 10);
            if (ep == optarg /* NOLINT */ || *ep != '\0'
                    || val <= 0 || val > 65535) {
                (void)fprintf(stderr,
                              "[adminclient] -p must be a port number (1-65535)\n");
                print_usage();
                return 1;
            }
            port = (int)val;
            break;
        }

        case 'c':
            if (snprintf(cmd, sizeof(cmd),
                         "%s", optarg /* NOLINT(misc-include-cleaner) */) < 0) {
                (void)fprintf(stderr, "[adminclient] command too long\n");
                return 1;
            }
            break;

        default:
            print_usage();
            return 1;
        }
    }

    if (host[0] == '\0' || port == 0 || cmd[0] == '\0') {
        (void)fprintf(stderr,
                      "[adminclient] -h, -p and -c are mandatory\n");
        print_usage();
        return 1;
    }

    if (!is_valid_command(cmd)) {
        (void)fprintf(stderr, "Unknown command: %s\n", cmd);
        return 1;
    }

    /* resolve host */
    struct addrinfo hints;
    (void)memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char portstr[16];
    if (snprintf(portstr, sizeof(portstr), "%d", port) < 0) { return 1; }

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || res == NULL) {
        (void)fprintf(stderr,
                      "[adminclient] cannot resolve host: %s\n", host);
        return 1;
    }

    int sfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sfd == -1) {
        char errbuf[ERRBUF_SIZE];
        (void)strerror_r(errno, errbuf, sizeof(errbuf));
        (void)fprintf(stderr, "[adminclient] socket: %s\n", errbuf);
        freeaddrinfo(res);
        return 1;
    }

    if (connect(sfd, res->ai_addr, res->ai_addrlen) == -1) {
        char errbuf[ERRBUF_SIZE];
        (void)strerror_r(errno, errbuf, sizeof(errbuf));
        (void)fprintf(stderr, "[adminclient] connect: %s\n", errbuf);
        freeaddrinfo(res);
        (void)close(sfd);
        return 1;
    }
    freeaddrinfo(res);

    /* send ADM|<COMMAND>\n */
    char msg[PROTO_BUF_SIZE];
    int  mlen = snprintf(msg, sizeof(msg), "ADM|%s\n", cmd);
    if (mlen < 0 || (size_t)mlen >= sizeof(msg)) {
        (void)close(sfd);
        return 1;
    }
    if (send_all(sfd, msg, (size_t)mlen) != 0) {
        (void)fprintf(stderr, "[adminclient] send failed\n");
        (void)close(sfd);
        return 1;
    }

    /* read response until connection closes */
    char    buf[RESP_BUF];
    ssize_t nr = 0;
    while ((nr = read(sfd, buf, sizeof(buf) - 1)) > 0) {
        buf[nr] = '\0';
        (void)printf("%s", buf);
    }

    (void)close(sfd);
    return 0;
}
