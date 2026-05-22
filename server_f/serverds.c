/* server_f/serverds.c – server entry point, accept loop, and job queue implementation */
#include "../include/server.h"
#include "../include/config.h"
#include "serverds.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>

#define USAGE_MSG    "Usage: ./server -f <config_file>\n"
#define ERRBUF_SIZE  64
#define CFG_PATH_MAX 512

/* ── globals ─────────────────────────────────────────────────────────────── */
Job                  *g_job_queue     = NULL;
volatile sig_atomic_t g_shutdown_flag = 0;
ServerConfig          g_server_cfg    = {0, 0, 0};

/* ── job queue ───────────────────────────────────────────────────────────── */

int next_job_id(void)
{
    static int counter = 0;
    return ++counter;
}

Job *queue_push(Job **head, int job_id, const char *video_path,
                int chunk_size, int threshold)
{
    if (!head || !video_path) { return NULL; }

    Job *j = (Job *)malloc(sizeof(Job));
    if (!j) { return NULL; }

    j->job_id     = job_id;
    j->chunk_size = chunk_size;
    j->threshold  = threshold;
    j->status     = JOB_QUEUED;
    j->progress   = 0;
    j->next       = NULL;

    if (snprintf(j->video_path, sizeof(j->video_path), "%s", video_path) < 0) {
        free(j);
        return NULL;
    }

    if (*head == NULL) {
        *head = j;
    } else {
        Job *cur = *head;
        while (cur->next != NULL) { cur = cur->next; }
        cur->next = j;
    }
    return j;
}

Job *queue_pop(Job **head)
{
    if (!head || !*head) { return NULL; }

    Job *cur  = *head;
    Job *prev = NULL;
    while (cur != NULL) {
        if (cur->status == JOB_QUEUED) {
            if (prev == NULL) { *head = cur->next; }
            else              { prev->next = cur->next; }
            cur->next = NULL;
            return cur;
        }
        prev = cur;
        cur  = cur->next;
    }
    return NULL;
}

Job *queue_find(Job *head, int job_id)
{
    Job *cur = head;
    while (cur != NULL) {
        if (cur->job_id == job_id) { return cur; }
        cur = cur->next;
    }
    return NULL;
}

void queue_free(Job **head)
{
    if (!head) { return; }
    Job *cur = *head;
    while (cur != NULL) {
        Job *nxt = cur->next;
        free(cur);
        cur = nxt;
    }
    *head = NULL;
}

/* ── server helpers ──────────────────────────────────────────────────────── */

static void print_usage(void)
{
    (void)fprintf(stderr, USAGE_MSG);
}

static void reap_children(void)
{
    int status = 0;
    while (waitpid(-1, &status, WNOHANG) > 0) { /* reap zombies */ }
}

static int setup_server_socket(int *out_fd)
{
    char errbuf[ERRBUF_SIZE];

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == -1) {
        (void)strerror_r(errno, errbuf, sizeof(errbuf));
        (void)fprintf(stderr, "[server] socket: %s\n", errbuf);
        return -1;
    }

    int optval = 1;
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, //NOLINT
                   &optval, sizeof(optval)) == -1) {
        (void)strerror_r(errno, errbuf, sizeof(errbuf));
        (void)fprintf(stderr, "[server] setsockopt: %s\n", errbuf);
        (void)close(sfd);
        return -1;
    }

    struct sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)SERVER_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    (void)memset(addr.sin_zero, 0, sizeof(addr.sin_zero));

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        (void)strerror_r(errno, errbuf, sizeof(errbuf));
        (void)fprintf(stderr, "[server] bind: %s\n", errbuf);
        (void)close(sfd);
        return -1;
    }

    if (listen(sfd, SERVER_BACKLOG) == -1) {
        (void)strerror_r(errno, errbuf, sizeof(errbuf));
        (void)fprintf(stderr, "[server] listen: %s\n", errbuf);
        (void)close(sfd);
        return -1;
    }

    *out_fd = sfd;
    return 0;
}

static void accept_loop(int server_fd, int max_workers)
{
    char errbuf[ERRBUF_SIZE];
    char client_ip[INET_ADDRSTRLEN];

    while (g_shutdown_flag == 0) {
        reap_children();

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        (void)memset(&client_addr, 0, sizeof(client_addr));

        int cfd = accept(server_fd,
                         (struct sockaddr *)&client_addr, &client_len);
        if (cfd == -1) {
            if (errno == EINTR) { continue; }
            (void)strerror_r(errno, errbuf, sizeof(errbuf));
            (void)fprintf(stderr, "[server] accept: %s\n", errbuf);
            continue;
        }

        if (inet_ntop(AF_INET, &client_addr.sin_addr,
                      client_ip, sizeof(client_ip)) != NULL) {
            (void)fprintf(stdout,
                          "[server] accepted connection from %s\n", client_ip);
        }

        pid_t pid = fork(); //NOLINT
        if (pid == -1) { //NOLINT
            (void)strerror_r(errno, errbuf, sizeof(errbuf));
            (void)fprintf(stderr, "[server] fork: %s\n", errbuf);
            (void)close(cfd);
            continue;
        }

        // PROCES COPIL
        if (pid == 0) {
            if (close(server_fd) == -1) { _Exit(1); }
            server_handle_client(cfd, max_workers);
            if (close(cfd) == -1) { _Exit(1); }
            _Exit(0);
        }

        // PROCES PARINTE
        if (close(cfd) == -1) {
            (void)strerror_r(errno, errbuf, sizeof(errbuf));
            (void)fprintf(stderr, "[server] close cfd: %s\n", errbuf);
        }
    }
}

int main(int argc, char *argv[])
{
    char cfg_path[CFG_PATH_MAX];
    cfg_path[0] = '\0';

    int opt = 0;
    while ((opt = getopt(argc, argv, "f:")) != -1) { //NOLINT
        switch (opt) {
        case 'f':
            if (snprintf(cfg_path, sizeof(cfg_path),
                         "%s", optarg /* NOLINT(misc-include-cleaner) */) < 0) {
                (void)fprintf(stderr, "[server] config path too long\n");
                return 1;
            }
            break;
        default:
            print_usage();
            return 1;
        }
    }

    if (cfg_path[0] == '\0') {
        print_usage();
        return 1;
    }

    if (access(cfg_path, R_OK) != 0) {
        (void)fprintf(stderr,
                      "[server] cannot read config file: %s\n", cfg_path);
        return 1;
    }

    if (config_load(cfg_path, &g_server_cfg) != 0) {
        (void)fprintf(stderr,
                      "[server] failed to load config: %s\n", cfg_path);
        return 1;
    }

    (void)fprintf(stdout,
                  "[server] config: max_workers=%d chunk_size=%d threshold=%d\n",
                  g_server_cfg.max_workers,
                  g_server_cfg.chunk_size,
                  g_server_cfg.threshold);

    int server_fd = -1;
    if (setup_server_socket(&server_fd) != 0) { return 1; }

    (void)fprintf(stdout, "[server] listening on port %d\n", SERVER_PORT);

    accept_loop(server_fd, g_server_cfg.max_workers);

    queue_free(&g_job_queue);
    (void)close(server_fd);
    return 0;
}