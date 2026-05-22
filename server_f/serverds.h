/* server_f/serverds.h – job queue types and management functions */
#ifndef SERVERDS_H
#define SERVERDS_H

#include <signal.h>
#include "../include/config.h"

/* ── job status ─────────────────────────────────────────────────────────── */
typedef enum {
    JOB_QUEUED  = 0,
    JOB_RUNNING = 1,
    JOB_DONE    = 2,
    JOB_ERROR   = 3
} JobStatus;

/* ── job struct ─────────────────────────────────────────────────────────── */
typedef struct Job {
    int         job_id;
    char        video_path[4096];
    int         chunk_size;
    int         threshold;
    int         status;    /* one of JobStatus values */
    int         progress;  /* 0–100 */
    struct Job *next;
} Job;

/* ── global state (defined in serverds.c) ─────────────────────────────── */
extern Job                  *g_job_queue;
extern volatile sig_atomic_t g_shutdown_flag;
extern ServerConfig          g_server_cfg;

/* ── queue operations ──────────────────────────────────────────────────── */
Job *queue_push(Job **head, int job_id, const char *video_path,
                int chunk_size, int threshold);
Job *queue_pop(Job **head);
Job *queue_find(Job *head, int job_id);
void queue_free(Job **head);
int  next_job_id(void);

#endif /* SERVERDS_H */
