#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum { BUFSZ = 4096 };

typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t  can_put;
    pthread_cond_t  can_get;

    bool full;      // в буфере лежат данные
    bool done;      // writer закончил нормально
    bool stop;      // фатальная ошибка: всем завершаться

    size_t len;
    char   buf[BUFSZ];

    int err_no;           // errno ошибки
    const char* err_ctx;  // файл/контекст ("stdout", путь, ...)
} Monitor;

typedef struct {
    int argc;
    char** argv;
    Monitor* mon;
} WriterArgs;

static void mon_init(Monitor* m) {
    memset(m, 0, sizeof(*m));
    pthread_mutex_init(&m->mtx, NULL);
    pthread_cond_init(&m->can_put, NULL);
    pthread_cond_init(&m->can_get, NULL);
}

static void mon_destroy(Monitor* m) {
    pthread_cond_destroy(&m->can_get);
    pthread_cond_destroy(&m->can_put);
    pthread_mutex_destroy(&m->mtx);
}

static void mon_fail_locked(Monitor* m, int err_no, const char* ctx) {
    if (!m->stop) {              // фиксируем первую ошибку
        m->stop = true;
        m->err_no = err_no;
        m->err_ctx = ctx;
    }
    pthread_cond_broadcast(&m->can_put);
    pthread_cond_broadcast(&m->can_get);
}

static ssize_t read_retry(int fd, void* buf, size_t cap) {
    for (;;) {
        ssize_t r = read(fd, buf, cap);
        if (r < 0 && errno == EINTR) continue;
        return r;
    }
}

static int write_all(int fd, const void* buf, size_t n) {
    const char* p = (const char*)buf;
    size_t left = n;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += (size_t)w;
        left -= (size_t)w;
    }
    return 0;
}

static void copy_fd_to_monitor(Monitor* m, int fd, const char* ctx_name) {
    char tmp[BUFSZ];

    for (;;) {
        ssize_t r = read_retry(fd, tmp, sizeof(tmp));
        if (r == 0) break;                 // EOF
        if (r < 0) {                       // read error
            pthread_mutex_lock(&m->mtx);
            mon_fail_locked(m, errno, ctx_name);
            pthread_mutex_unlock(&m->mtx);
            return;
        }

        pthread_mutex_lock(&m->mtx);
        while (m->full && !m->stop) {
            pthread_cond_wait(&m->can_put, &m->mtx);
        }
        if (m->stop) {                     // кто-то уже упал
            pthread_mutex_unlock(&m->mtx);
            return;
        }

        memcpy(m->buf, tmp, (size_t)r);
        m->len = (size_t)r;
        m->full = true;
        pthread_cond_signal(&m->can_get);
        pthread_mutex_unlock(&m->mtx);
    }
}

static void* writer_thread(void* arg) {
    WriterArgs* wa = (WriterArgs*)arg;
    Monitor* m = wa->mon;

    if (wa->argc == 1) {
        copy_fd_to_monitor(m, STDIN_FILENO, "stdin");
    }
    else {
        for (int i = 1; i < wa->argc; ++i) {
            int fd = open(wa->argv[i], O_RDONLY);
            if (fd < 0) {
                pthread_mutex_lock(&m->mtx);
                mon_fail_locked(m, errno, wa->argv[i]);
                pthread_mutex_unlock(&m->mtx);
                break;
            }
            copy_fd_to_monitor(m, fd, wa->argv[i]);
            close(fd);

            pthread_mutex_lock(&m->mtx);
            bool stop = m->stop;
            pthread_mutex_unlock(&m->mtx);
            if (stop) break;
        }
    }

    pthread_mutex_lock(&m->mtx);
    m->done = true;
    pthread_cond_broadcast(&m->can_get);
    pthread_mutex_unlock(&m->mtx);
    return NULL;
}

static void* reader_thread(void* arg) {
    Monitor* m = (Monitor*)arg;
    char local[BUFSZ];

    for (;;) {
        pthread_mutex_lock(&m->mtx);
        while (!m->full && !m->done && !m->stop) {
            pthread_cond_wait(&m->can_get, &m->mtx);
        }

        if (m->stop) {
            pthread_mutex_unlock(&m->mtx);
            return NULL;
        }

        if (!m->full && m->done) {
            pthread_mutex_unlock(&m->mtx);
            return NULL;
        }

        size_t n = m->len;
        memcpy(local, m->buf, n);
        m->full = false;
        pthread_cond_signal(&m->can_put);
        pthread_mutex_unlock(&m->mtx);

        if (write_all(STDOUT_FILENO, local, n) < 0) {
            pthread_mutex_lock(&m->mtx);
            mon_fail_locked(m, errno, "stdout");
            pthread_mutex_unlock(&m->mtx);
            return NULL;
        }
    }
}

int main(int argc, char* argv[]) {
    Monitor mon;
    mon_init(&mon);

    WriterArgs wa = { .argc = argc, .argv = argv, .mon = &mon };

    pthread_t tw, tr;
    if (pthread_create(&tw, NULL, writer_thread, &wa) != 0 ||
        pthread_create(&tr, NULL, reader_thread, &mon) != 0) {
        fprintf(stderr, "pthread_create: %s\n", strerror(errno));
        mon_destroy(&mon);
        return 1;
    }

    pthread_join(tw, NULL);
    pthread_join(tr, NULL);

    int rc = 0;
    pthread_mutex_lock(&mon.mtx);
    if (mon.stop) {
        fprintf(stderr, "%s: %s\n", mon.err_ctx ? mon.err_ctx : "error",
            strerror(mon.err_no ? mon.err_no : EIO));
        rc = 1;
    }
    pthread_mutex_unlock(&mon.mtx);

    mon_destroy(&mon);
    return rc;
}
