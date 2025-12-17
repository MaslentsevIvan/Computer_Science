#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    MT_LOCK = 1,   // один "жетон" лидерства
    MT_REG = 2,   // регистрации (читает только дирижёр)
    MT_ACK = 3    // ACK/BYE (читает только дирижёр)
};

static long MT_TO(pid_t pid) {
    // личный "почтовый ящик" процесса
    return (long)pid + 1000L;
}

enum kind {
    K_REG = 1,
    K_ASSIGN = 2,
    K_TURN = 3,
    K_DONE = 4,
    K_ACK = 5,
    K_BYE = 6,
    K_LOCK = 7
};

struct msg {
    long mtype;      // тип сообщения
    int  kind;       // что это за сообщение
    pid_t pid;       // отправитель/получатель (по смыслу)
    int  seq;        // номер позиции (для ACK)
    unsigned char ch;// символ
};

static void die(const char* what) {
    perror(what);
    exit(EXIT_FAILURE);
}

static void xmsgsnd(int q, const struct msg* m, int flags) {
    for (;;) {
        if (msgsnd(q, m, sizeof(*m) - sizeof(m->mtype), flags) == 0) return;
        if (errno == EINTR) continue;
        die("msgsnd");
    }
}

static ssize_t xmsgrcv(int q, struct msg* m, long type, int flags) {
    for (;;) {
        ssize_t r = msgrcv(q, m, sizeof(*m) - sizeof(m->mtype), type, flags);
        if (r >= 0) return r;
        if (errno == EINTR) continue;
        return -1; // пусть вызывающий решит, что делать
    }
}

static int cmp_pid(const void* a, const void* b) {
    pid_t pa = *(const pid_t*)a;
    pid_t pb = *(const pid_t*)b;
    return (pa > pb) - (pa < pb);
}

static size_t collect_unique(const unsigned char* s, unsigned char* out, size_t out_cap) {
    int seen[256] = { 0 };
    size_t k = 0;
    for (size_t i = 0; s[i] != '\0'; i++) {
        unsigned char c = s[i];
        if (!seen[c]) {
            seen[c] = 1;
            if (k < out_cap) out[k++] = c;
            else return k; // переполнение (не должно случиться)
        }
    }
    return k;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s N [SONG]\n"
            "If SONG not given, reads one line from stdin.\n", argv[0]);
        return 1;
    }

    int n = atoi(argv[1]);
    if (n <= 0) {
        fprintf(stderr, "N must be positive\n");
        return 1;
    }

    char* song = NULL;
    if (argc >= 3) {
        song = strdup(argv[2]);
        if (!song) die("strdup");
    }
    else {
        size_t cap = 0;
        ssize_t len = getline(&song, &cap, stdin);
        if (len < 0) die("getline");
        // Оставим '\n', если он есть — это часть "песни"
    }

    // Очередь сообщений создаёт "запускающий" процесс (это НЕ дирижёр по логике).
    int q = msgget(IPC_PRIVATE, 0600 | IPC_CREAT);
    if (q < 0) die("msgget");

    // Кладём один LOCK-жетон: кто его снимет — станет дирижёром.
    struct msg lock = { .mtype = MT_LOCK, .kind = K_LOCK, .pid = 0, .seq = 0, .ch = 0 };
    xmsgsnd(q, &lock, 0);

    // Форкаем ещё n-1 процессов
    for (int i = 1; i < n; i++) {
        pid_t pid = fork();
        if (pid < 0) die("fork");
        if (pid == 0) break; // ребёнок выходит из цикла и идёт в общий код
    }

    // Начиная отсюда — один и тот же алгоритм у всех процессов
    setvbuf(stdout, NULL, _IONBF, 0);

    pid_t self = getpid();

    // 1) Регистрируемся
    struct msg reg = { .mtype = MT_REG, .kind = K_REG, .pid = self, .seq = 0, .ch = 0 };
    xmsgsnd(q, &reg, 0);

    // 2) Пытаемся стать дирижёром: кто первый забрал LOCK — тот дирижёр
    int is_conductor = 0;
    struct msg tmp;

    // небольшая "разбежка", чтобы дирижёр не всегда был одним и тем же
    usleep((useconds_t)((self % 50) * 1000));

    if (xmsgrcv(q, &tmp, MT_LOCK, IPC_NOWAIT) >= 0 && tmp.kind == K_LOCK) {
        is_conductor = 1;
    }

    unsigned char my_ch = 0;     // 0 => "молчу"
    pid_t conductor_pid = -1;    // только для ясности (в workers не нужен)

    if (is_conductor) {
        conductor_pid = self;

        // 3) Собираем всех участников
        pid_t* pids = calloc((size_t)n, sizeof(pid_t));
        if (!pids) die("calloc pids");

        for (int i = 0; i < n; i++) {
            struct msg m;
            if (xmsgrcv(q, &m, MT_REG, 0) < 0) die("msgrcv REG");
            if (m.kind != K_REG) { fprintf(stderr, "bad REG\n"); exit(2); }
            pids[i] = m.pid;
        }

        // Чтобы назначение было детерминированным:
        qsort(pids, (size_t)n, sizeof(pid_t), cmp_pid);

        // 4) Уникальные символы песни
        unsigned char uniq[256];
        size_t ucnt = collect_unique((unsigned char*)song, uniq, 256);

        if ((int)ucnt > n) {
            fprintf(stderr, "ERROR: unique symbols (%zu) > processes (%d)\n", ucnt, n);
            // аварийно завершим всех
            for (int i = 0; i < n; i++) {
                struct msg done = { .mtype = MT_TO(pids[i]), .kind = K_DONE, .pid = conductor_pid };
                xmsgsnd(q, &done, 0);
            }
            msgctl(q, IPC_RMID, NULL);
            free(pids);
            free(song);
            return 3;
        }

        // owner[c] = pid владельца символа c, или -1
        pid_t owner[256];
        for (int c = 0; c < 256; c++) owner[c] = (pid_t)-1;

        for (size_t i = 0; i < ucnt; i++) {
            owner[uniq[i]] = pids[i];
        }

        // 5) Рассылаем ASSIGN каждому
        for (int i = 0; i < n; i++) {
            unsigned char assigned = 0;
            // найдём символ, который принадлежит этому pid (если есть)
            for (size_t k = 0; k < ucnt; k++) {
                if (owner[uniq[k]] == pids[i]) { assigned = uniq[k]; break; }
            }

            struct msg asg = {
                .mtype = MT_TO(pids[i]),
                .kind = K_ASSIGN,
                .pid = conductor_pid,
                .seq = 0,
                .ch = assigned
            };
            xmsgsnd(q, &asg, 0);

            if (pids[i] == self) my_ch = assigned;
        }

        // 6) "Пение": идём по песне, выдаём TURN владельцу символа и ждём ACK
        for (int seq = 0; song[seq] != '\0'; seq++) {
            unsigned char c = (unsigned char)song[seq];
            pid_t singer = owner[c];
            if (singer == (pid_t)-1) continue; // на всякий случай

            if (singer == self) {
                // дирижёр поёт свой символ сам
                if (my_ch != 0) (void)write(STDOUT_FILENO, &c, 1);
                continue;
            }

            struct msg turn = {
                .mtype = MT_TO(singer),
                .kind = K_TURN,
                .pid = conductor_pid,
                .seq = seq,
                .ch = c
            };
            xmsgsnd(q, &turn, 0);

            struct msg ack;
            if (xmsgrcv(q, &ack, MT_ACK, 0) < 0) die("msgrcv ACK");
            if (ack.kind != K_ACK || ack.seq != seq) {
                fprintf(stderr, "bad ACK (kind=%d seq=%d expected seq=%d)\n", ack.kind, ack.seq, seq);
                exit(4);
            }
        }

        size_t L = strlen(song);
        if (L == 0 || song[L - 1] != '\n') {
            const char nl = '\n';
            write(STDOUT_FILENO, &nl, 1);
        }


        // 7) Завершение: DONE всем + ждём BYE от остальных
        for (int i = 0; i < n; i++) {
            if (pids[i] == self) continue;
            struct msg done = { .mtype = MT_TO(pids[i]), .kind = K_DONE, .pid = conductor_pid };
            xmsgsnd(q, &done, 0);
        }

        int bye_need = n - 1;
        while (bye_need > 0) {
            struct msg bye;
            if (xmsgrcv(q, &bye, MT_ACK, 0) < 0) die("msgrcv BYE");
            if (bye.kind == K_BYE) bye_need--;
        }

        // Удаляем очередь
        msgctl(q, IPC_RMID, NULL);

        free(pids);
        free(song);
        return 0;
    }

    // ---------------- WORKER ----------------

    // Ждём ASSIGN
    struct msg asg;
    if (xmsgrcv(q, &asg, MT_TO(self), 0) < 0) die("msgrcv ASSIGN");
    if (asg.kind != K_ASSIGN) { fprintf(stderr, "bad ASSIGN\n"); exit(5); }
    my_ch = asg.ch;

    // Основной цикл: ждём TURN / DONE
    for (;;) {
        struct msg m;
        if (xmsgrcv(q, &m, MT_TO(self), 0) < 0) die("msgrcv worker");

        if (m.kind == K_TURN) {
            // поём только свой символ; дирижёр присылает корректно, но проверим
            if (my_ch != 0 && m.ch == my_ch) {
                (void)write(STDOUT_FILENO, &m.ch, 1);
            }

            struct msg ack = {
                .mtype = MT_ACK,
                .kind = K_ACK,
                .pid = self,
                .seq = m.seq,
                .ch = 0
            };
            xmsgsnd(q, &ack, 0);
        }
        else if (m.kind == K_DONE) {
            struct msg bye = { .mtype = MT_ACK, .kind = K_BYE, .pid = self };
            xmsgsnd(q, &bye, 0);
            break;
        }
    }

    free(song);
    return 0;
}
