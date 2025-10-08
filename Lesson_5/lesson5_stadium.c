#define _XOPEN_SOURCE 700
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { ARRIVAL_TYPE = 1, JUDGE_TYPE = 2, BATON_BASE = 1000 };

struct msg {
    long mtype;
    int runner_id;
};

static void die(const char* where)
{
    perror(where);
    exit(1);
}

static long baton_type(int i)
{
    return BATON_BASE + i;
}

static void runner_proc(int qid, int id, int n)
{
    struct msg m = { .mtype = ARRIVAL_TYPE, .runner_id = id };
    if (msgsnd(qid, &m, sizeof(int), 0) == -1)
        die("msgsnd(arrival)");

    struct msg in;
    if (msgrcv(qid, &in, sizeof(int), baton_type(id), 0) == -1)
        die("msgrcv(baton)");

    /* 5 ms */
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 5 * 1000 * 1000 };
    nanosleep(&ts, NULL);

    struct msg out;
    out.runner_id = id;
    if (id < n)
        out.mtype = baton_type(id + 1);
    else
        out.mtype = JUDGE_TYPE;

    if (msgsnd(qid, &out, sizeof(int), 0) == -1)
        die("msgsnd(pass)");

    _exit(0);
}

static double elapsed_ms(struct timespec a, struct timespec b)
{
    long sec = b.tv_sec - a.tv_sec;
    long nsec = b.tv_nsec - a.tv_nsec;
    return (double)sec * 1000.0 + (double)nsec / 1.0e6;
}

int main(int argc, char** argv)
{
    int i, n, qid, arrived;
    struct timespec t0, t1;
    struct msg start, fin;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <num_runners>\n", argv[0]);
        return 1;
    }

    n = atoi(argv[1]);
    if (n <= 0) {
        fprintf(stderr, "num_runners must be > 0\n");
        return 1;
    }

    qid = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    if (qid == -1)
        die("msgget");

    for (i = 1; i <= n; ++i) {
        pid_t pid = fork();
        if (pid < 0)
            die("fork");
        if (pid == 0)
            runner_proc(qid, i, n);
    }

    arrived = 0;
    while (arrived < n) {
        struct msg m;
        if (msgrcv(qid, &m, sizeof(int), ARRIVAL_TYPE, 0) == -1)
            die("msgrcv(arrival)");
        arrived++;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &t0) == -1)
        die("clock_gettime");

    start.mtype = baton_type(1);
    start.runner_id = 0;
    if (msgsnd(qid, &start, sizeof(int), 0) == -1)
        die("msgsnd(start)");

    if (msgrcv(qid, &fin, sizeof(int), JUDGE_TYPE, 0) == -1)
        die("msgrcv(finish)");

    if (clock_gettime(CLOCK_MONOTONIC, &t1) == -1)
        die("clock_gettime");

    printf("[SysV] N=%d, время полного круга: %.3f ms\n",
        n, elapsed_ms(t0, t1));

    if (msgctl(qid, IPC_RMID, NULL) == -1)
        die("msgctl(IPC_RMID)");

    for (i = 0; i < n; ++i)
        wait(NULL);

    return 0;
}