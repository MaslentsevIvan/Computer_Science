#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NAMEBUF 64

static void die(const char* where)
{
    perror(where);
    exit(1);
}

static void qname_runner(char* buf, size_t sz, pid_t base, int i)
{
    snprintf(buf, sz, "/runner_%d_%d", (int)base, i);
}

static void qname_judge(char* buf, size_t sz, pid_t base)
{
    snprintf(buf, sz, "/judge_%d", (int)base);
}

static double elapsed_ms(struct timespec a, struct timespec b)
{
    long sec = b.tv_sec - a.tv_sec;
    long nsec = b.tv_nsec - a.tv_nsec;
    return (double)sec * 1000.0 + (double)nsec / 1.0e6;
}

static void runner_proc(pid_t base, int id, int n, long msgsize)
{
    char myq[NAMEBUF], nextq[NAMEBUF], jq[NAMEBUF];

    qname_runner(myq, sizeof(myq), base, id);
    qname_judge(jq, sizeof(jq), base);
    if (id < n) {
        qname_runner(nextq, sizeof(nextq), base, id + 1);
    }

    mqd_t q_my = mq_open(myq, O_RDONLY);
    if (q_my == (mqd_t)-1) {
        die("mq_open(my)");
    }

    mqd_t q_next = (mqd_t)-1;
    if (id < n) {
        q_next = mq_open(nextq, O_WRONLY);
        if (q_next == (mqd_t)-1) {
            die("mq_open(next)");
        }
    }

    mqd_t q_judge = mq_open(jq, O_WRONLY);
    if (q_judge == (mqd_t)-1) {
        die("mq_open(judge)");
    }

    int payload = id;
    if (mq_send(q_judge, (const char*)&payload, sizeof(payload), 0) == -1) {
        die("mq_send(arrival)");
    }

    int baton = -1;
    if (mq_receive(q_my, (char*)&baton, msgsize, NULL) == -1) {
        die("mq_receive(baton)");
    }

    struct timespec ts = { .tv_sec = 0, .tv_nsec = 5 * 1000 * 1000 };
    nanosleep(&ts, NULL);

    if (id < n) {
        if (mq_send(q_next, (const char*)&payload, sizeof(payload), 0) == -1) {
            die("mq_send(next)");
        }
    }
    else {
        if (mq_send(q_judge, (const char*)&payload, sizeof(payload), 0) == -1) {
            die("mq_send(finish)");
        }
    }

    mq_close(q_my);
    if (q_next != (mqd_t)-1) {
        mq_close(q_next);
    }
    mq_close(q_judge);
    _exit(0);
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <num_runners>\n", argv[0]);
        return 1;
    }

    int n = atoi(argv[1]);
    if (n <= 0) {
        fprintf(stderr, "num_runners must be > 0\n");
        return 1;
    }

    pid_t base = getpid();

    struct mq_attr attr = { 0 };
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(int);
    long msgsize = attr.mq_msgsize;

    char jq[NAMEBUF];
    qname_judge(jq, sizeof(jq), base);
    mqd_t q_judge = mq_open(jq, O_CREAT | O_RDONLY, 0600, &attr);
    if (q_judge == (mqd_t)-1) {
        die("mq_open(judge create)");
    }

    char rname[NAMEBUF];
    for (int i = 1; i <= n; ++i) {
        qname_runner(rname, sizeof(rname), base, i);
        mqd_t q = mq_open(rname, O_CREAT | O_RDONLY, 0600, &attr);
        if (q == (mqd_t)-1) {
            die("mq_open(runner create)");
        }
        mq_close(q);
    }

    for (int i = 1; i <= n; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            die("fork");
        }
        if (pid == 0) {
            runner_proc(base, i, n, msgsize);
        }
    }

    int arrived = 0;
    while (arrived < n) {
        int who = -1;
        if (mq_receive(q_judge, (char*)&who, msgsize, NULL) == -1) {
            die("mq_receive(arrival)");
        }
        arrived++;
    }

    struct timespec t0, t1;
    if (clock_gettime(CLOCK_MONOTONIC, &t0) == -1) {
        die("clock_gettime");
    }

    char firstq[NAMEBUF];
    qname_runner(firstq, sizeof(firstq), base, 1);
    mqd_t q_first = mq_open(firstq, O_WRONLY);
    if (q_first == (mqd_t)-1) {
        die("mq_open(first runner WR)");
    }

    int baton = 1;
    if (mq_send(q_first, (const char*)&baton, sizeof(baton), 0) == -1) {
        die("mq_send(start)");
    }
    mq_close(q_first);

    int fin = -1;
    if (mq_receive(q_judge, (char*)&fin, msgsize, NULL) == -1) {
        die("mq_receive(finish)");
    }
    if (clock_gettime(CLOCK_MONOTONIC, &t1) == -1) {
        die("clock_gettime");
    }

    printf("[POSIX] N=%d, время полного круга: %.3f ms\n", n, elapsed_ms(t0, t1));

    mq_close(q_judge);
    mq_unlink(jq);

    for (int i = 1; i <= n; ++i) {
        qname_runner(rname, sizeof(rname), base, i);
        mq_unlink(rname);
    }

    for (int i = 0; i < n; ++i) {
        wait(NULL);
    }

    return 0;
}