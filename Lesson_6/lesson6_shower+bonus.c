#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <signal.h>

#define SEM_KEY 0x12345
#define SHM_KEY 0x23456

enum { SEM_MUTEX = 0, SEM_CAP = 1, SEM_MQ = 2, SEM_WQ = 3, SEM_COUNT = 4 };
enum { NONE = 0, MEN = 1, WOMEN = 2 };

typedef struct {
    int men_in, women_in;
    int men_wait, women_wait;
    int current_gender;
    int men_streak;
    int women_streak;
    int turn;
    int N;
} state_t;

static int semid = -1, shmid = -1;
static state_t* S = NULL;

static void die(const char* msg) {
    perror(msg);
    exit(1);
}

static void msleep(int ms) {
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
    }
}

static int get_max_streak(void) {
    const char* e = getenv("MAX_STREAK");
    if (e && *e) {
        int v = atoi(e);
        if (v > 0) return v;
    }
    return (S && S->N > 0) ? (2 * S->N) : 4;
}


static void P_op_undo(unsigned short semnum, short delta) {
    struct sembuf op = { .sem_num = semnum, .sem_op = delta, .sem_flg = SEM_UNDO };
    if (semop(semid, &op, 1) < 0) die("semop undo");
}

static void V_op_undo(unsigned short semnum, short delta) {
    struct sembuf op = { .sem_num = semnum, .sem_op = delta, .sem_flg = SEM_UNDO };
    if (semop(semid, &op, 1) < 0) die("semop undo");
}

static void P(unsigned short s) {
    P_op_undo(s, -1);
}

static void V(unsigned short s) {
    V_op_undo(s, +1);
}

static void Pq(unsigned short s) {
    struct sembuf op = { .sem_num = s, .sem_op = -1, .sem_flg = 0 };
    if (semop(semid, &op, 1) < 0) die("semop queue P");
}

static void Vq(unsigned short s) {
    struct sembuf op = { .sem_num = s, .sem_op = +1, .sem_flg = 0 };
    if (semop(semid, &op, 1) < 0) die("semop queue V");
}

static void Vq_k(unsigned short s, unsigned k) {
    if (!k) return;
    struct sembuf op = { .sem_num = s, .sem_op = (short)k, .sem_flg = 0 };
    if (semop(semid, &op, 1) < 0) die("semop queue V_k");
}

static void cmd_init(int N) {
    semid = semget(SEM_KEY, SEM_COUNT, IPC_CREAT | IPC_EXCL | 0666);
    if (semid < 0) die("semget");
    if (semctl(semid, SEM_MUTEX, SETVAL, 1) < 0) die("semctl mutex");
    if (semctl(semid, SEM_CAP, SETVAL, N) < 0) die("semctl cap");
    if (semctl(semid, SEM_MQ, SETVAL, 0) < 0) die("semctl menQ");
    if (semctl(semid, SEM_WQ, SETVAL, 0) < 0) die("semctl womenQ");

    shmid = shmget(SHM_KEY, sizeof(state_t), IPC_CREAT | IPC_EXCL | 0666);
    if (shmid < 0) die("shmget");
    S = (state_t*)shmat(shmid, NULL, 0);
    if (S == (void*)-1) die("shmat");
    memset(S, 0, sizeof(*S));
    S->N = N;
    S->current_gender = NONE;
    S->turn = MEN;
    S->men_streak = 0;
    S->women_streak = 0;
    printf("[init] N=%d OK (SEM_KEY=0x%x, SHM_KEY=0x%x)\n", N, SEM_KEY, SHM_KEY);
}

static void attach_all(void) {
    semid = semget(SEM_KEY, SEM_COUNT, 0666);
    if (semid < 0) die("semget attach");
    shmid = shmget(SHM_KEY, sizeof(state_t), 0666);
    if (shmid < 0) die("shmget attach");
    S = (state_t*)shmat(shmid, NULL, 0);
    if (S == (void*)-1) die("shmat attach");
}

static void cmd_destroy(void) {
    int sid = semget(SEM_KEY, 0, 0666);
    if (sid >= 0) semctl(sid, 0, IPC_RMID);
    int mid = shmget(SHM_KEY, 0, 0666);
    if (mid >= 0) shmctl(mid, IPC_RMID, NULL);
    if (S && S != (void*)-1) shmdt(S);
    printf("[destroy] resources removed (if existed)\n");
}

static void cmd_force_init(int N) {
    int sid = semget(SEM_KEY, 0, 0666);
    if (sid >= 0) semctl(sid, 0, IPC_RMID);
    int mid = shmget(SHM_KEY, 0, 0666);
    if (mid >= 0) shmctl(mid, IPC_RMID, NULL);
    cmd_init(N);
}

static const char* g2s(int g) {
    return g == MEN ? "MEN" : g == WOMEN ? "WOMEN" : "NONE";
}

static void log_state_locked(const char* tag, int who) {
    int cap_calc = S->N - (S->men_in + S->women_in);
    if (cap_calc < 0) cap_calc = 0;
    fprintf(stderr,
        "[%s][pid=%d %s] in: M=%d W=%d | wait: M=%d W=%d | cur=%s cap=%d turn=%s streakM=%d streakW=%d\n",
        tag, getpid(), who == MEN ? "MEN" : "WOMEN",
        S->men_in, S->women_in, S->men_wait, S->women_wait,
        g2s(S->current_gender), cap_calc,
        g2s(S->turn), S->men_streak, S->women_streak);
}

static void enter_gender(int who) {
    for (;;) {
        P(SEM_MUTEX);

        int MAX_STREAK = get_max_streak();
        bool other_inside = (who == MEN ? S->women_in > 0 : S->men_in > 0);
        bool cur_none_or_me =
            (S->current_gender == NONE ||
                S->current_gender == (who == MEN ? MEN : WOMEN));

        bool fairness_block = false;
        if (who == MEN) {
            fairness_block =
                (S->men_streak >= MAX_STREAK &&
                    S->women_wait > 0 &&
                    S->current_gender != WOMEN);
        }
        else {
            fairness_block =
                (S->women_streak >= MAX_STREAK &&
                    S->men_wait > 0 &&
                    S->current_gender != MEN);
        }

        bool allow = cur_none_or_me && !other_inside && !fairness_block;

        if (allow) {
            V(SEM_MUTEX);

            P(SEM_CAP);

            P(SEM_MUTEX);
            MAX_STREAK = get_max_streak();

            bool other_inside2 = (who == MEN ? S->women_in > 0 : S->men_in > 0);
            bool cur_none_or_me2 =
                (S->current_gender == NONE ||
                    S->current_gender == (who == MEN ? MEN : WOMEN));

            bool fairness_block2 = false;
            if (who == MEN) {
                fairness_block2 =
                    (S->men_streak >= MAX_STREAK &&
                        S->women_wait > 0 &&
                        S->current_gender != WOMEN);
            }
            else {
                fairness_block2 =
                    (S->women_streak >= MAX_STREAK &&
                        S->men_wait > 0 &&
                        S->current_gender != MEN);
            }

            bool allow2 = cur_none_or_me2 && !other_inside2 && !fairness_block2;

            if (!allow2) {
                V(SEM_MUTEX);
                V(SEM_CAP);

                P(SEM_MUTEX);
                if (who == MEN) S->men_wait++; else S->women_wait++;
                log_state_locked("WAIT(recheck)", who);
                V(SEM_MUTEX);

                if (who == MEN) Pq(SEM_MQ); else Pq(SEM_WQ);
                continue;
            }

            S->current_gender = (who == MEN ? MEN : WOMEN);
            if (who == MEN) {
                S->men_in++;
                S->men_streak++;
                S->women_streak = 0;
            }
            else {
                S->women_in++;
                S->women_streak++;
                S->men_streak = 0;
            }

            log_state_locked("ENTER", who);
            V(SEM_MUTEX);
            break;

        }
        else {
            if (who == MEN) S->men_wait++; else S->women_wait++;
            log_state_locked("WAIT", who);
            V(SEM_MUTEX);

            if (who == MEN) Pq(SEM_MQ); else Pq(SEM_WQ);
        }
    }
}

static void wake_women_locked(const char* reason) {
    int k = S->women_wait;
    S->women_wait = 0;
    S->current_gender = WOMEN;
    S->turn = MEN;
    S->men_streak = 0;
    S->women_streak = 0;
    fprintf(stderr, "[SWITCH] cur=WOMEN woke=%d reason=%s\n", k, reason);
    Vq_k(SEM_WQ, (unsigned)k);
}

static void wake_men_locked(const char* reason) {
    int k = S->men_wait;
    S->men_wait = 0;
    S->current_gender = MEN;
    S->turn = WOMEN;
    S->men_streak = 0;
    S->women_streak = 0;
    fprintf(stderr, "[SWITCH] cur=MEN woke=%d reason=%s\n", k, reason);
    Vq_k(SEM_MQ, (unsigned)k);
}

static void leave_gender(int who) {
    P(SEM_MUTEX);

    if (who == MEN) S->men_in--; else S->women_in--;

    V(SEM_CAP);

    if (S->men_in == 0 && S->women_in == 0) {
        int MAX_STREAK = get_max_streak();

        if (S->women_wait > 0 && S->men_wait == 0) {
            wake_women_locked("only_women_wait");
        }
        else if (S->men_wait > 0 && S->women_wait == 0) {
            wake_men_locked("only_men_wait");

        }
        else if (S->women_wait > 0 && S->men_wait > 0) {
            if (S->men_streak >= MAX_STREAK && S->women_streak < MAX_STREAK) {
                wake_women_locked("men_streak_reached");
            }
            else if (S->women_streak >= MAX_STREAK && S->men_streak < MAX_STREAK) {
                wake_men_locked("women_streak_reached");
            }
            else if (S->turn == WOMEN) {
                wake_women_locked("turn");
            }
            else {
                wake_men_locked("turn");
            }
        }
        else {
            S->current_gender = NONE;
            S->men_streak = 0;
            S->women_streak = 0;
        }
    }

    log_state_locked("LEAVE", who);
    V(SEM_MUTEX);
}

static void client_once(int who, int ms_min, int ms_max) {
    int span, ms;

    enter_gender(who);
    span = (ms_max > ms_min ? ms_max - ms_min : 0);
    ms = ms_min + (span ? rand() % span : 0);
    msleep(ms);
    leave_gender(who);
}

static void spawn_many(int who, int count, int ms_min, int ms_max) {
    int i;

    attach_all();
    srand((unsigned)(time(NULL) ^ getpid()));
    for (i = 0; i < count; i++) {
        pid_t pid = fork();
        if (pid < 0) die("fork");
        if (pid == 0) {
            client_once(who, ms_min, ms_max);
            _exit(0);
        }
    }
    for (i = 0; i < count; i++) wait(NULL);
}

static void cmd_selftest(int N, int M, int W, int rounds) {
    int r;

    cmd_init(N);
    for (r = 0; r < rounds; r++) {
        fprintf(stderr, "\n=== ROUND %d ===\n", r + 1);
        if (M > 0) spawn_many(MEN, M, 300, 1200);
        if (W > 0) spawn_many(WOMEN, W, 300, 1200);
    }
    cmd_destroy();
}

static void usage(const char* argv0) {
    fprintf(stderr,
        "Usage:\n"
        "  %s init <N>\n"
        "  %s force-init <N>\n"
        "  %s run-men <K>\n"
        "  %s run-women <K>\n"
        "  %s run men <K> [women <K>]\n"
        "  %s run women <K> [men <K>]\n"
        "  %s selftest <N> <M> <W> <ROUNDS>\n"
        "  %s destroy\n"
        "\n"
        "Env: MAX_STREAK=<int>  (default: 2*N)\n",
        argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0);
    exit(2);
}

int main(int argc, char** argv) {
    if (argc < 2) usage(argv[0]);

    if (!strcmp(argv[1], "init")) {
        int N;
        if (argc != 3) usage(argv[0]);
        N = atoi(argv[2]);
        if (N <= 0) { fprintf(stderr, "N>0\n"); return 2; }
        cmd_init(N);
        return 0;

    }
    else if (!strcmp(argv[1], "force-init")) {
        int N;
        if (argc != 3) usage(argv[0]);
        N = atoi(argv[2]);
        if (N <= 0) { fprintf(stderr, "N>0\n"); return 2; }
        cmd_force_init(N);
        return 0;

    }
    else if (!strcmp(argv[1], "run-men")) {
        int K;
        if (argc != 3) usage(argv[0]);
        K = atoi(argv[2]);
        if (K < 0) K = 0;
        attach_all();
        spawn_many(MEN, K, 300, 1200);
        return 0;

    }
    else if (!strcmp(argv[1], "run-women")) {
        int K;
        if (argc != 3) usage(argv[0]);
        K = atoi(argv[2]);
        if (K < 0) K = 0;
        attach_all();
        spawn_many(WOMEN, K, 300, 1200);
        return 0;

    }
    else if (!strcmp(argv[1], "run")) {
        int Km = -1, Kw = -1;
        pid_t pm = -1, pw = -1;
        int i;

        if (argc < 4) usage(argv[0]);

        for (i = 2; i < argc; ) {
            if (!strcmp(argv[i], "men")) {
                if (i + 1 >= argc) usage(argv[0]);
                Km = atoi(argv[i + 1]);
                if (Km < 0) Km = 0;
                i += 2;
            }
            else if (!strcmp(argv[i], "women")) {
                if (i + 1 >= argc) usage(argv[0]);
                Kw = atoi(argv[i + 1]);
                if (Kw < 0) Kw = 0;
                i += 2;
            }
            else {
                usage(argv[0]);
            }
        }
        if (Km < 0 && Kw < 0) usage(argv[0]);

        attach_all();

        if (Km >= 0) {
            pm = fork();
            if (pm < 0) die("fork men");
            if (pm == 0) {
                spawn_many(MEN, Km, 300, 1200);
                _exit(0);
            }
        }
        if (Kw >= 0) {
            pw = fork();
            if (pw < 0) die("fork women");
            if (pw == 0) {
                spawn_many(WOMEN, Kw, 300, 1200);
                _exit(0);
            }
        }
        if (pm > 0) waitpid(pm, NULL, 0);
        if (pw > 0) waitpid(pw, NULL, 0);
        return 0;

    }
    else if (!strcmp(argv[1], "destroy")) {
        cmd_destroy();
        return 0;

    }
    else if (!strcmp(argv[1], "selftest")) {
        int N, M, W, R;
        if (argc != 6) usage(argv[0]);
        N = atoi(argv[2]);
        M = atoi(argv[3]);
        W = atoi(argv[4]);
        R = atoi(argv[5]);
        if (N <= 0 || M < 0 || W < 0 || R <= 0) usage(argv[0]);
        cmd_selftest(N, M, W, R);
        return 0;

    }
    else {
        usage(argv[0]);
    }
}