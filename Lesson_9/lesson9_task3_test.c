#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { SEM_MUTEX = 0, SEM_NUT_SLOTS = 1, SEM_SCREW_SLOTS = 2, SEM_COUNT = 3 };

typedef struct {
    int nuts;        // 0..2
    int screw;       // 0..1
    int device_id;   // текущий номер полуфабриката
    int completed;   // сколько устройств собрано
    int target;      // сколько нужно собрать
    int stop;        // флаг остановки
} Shared;

union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};

static void sem_op_retry(int semid, unsigned short semnum, short delta) {
    struct sembuf op = { .sem_num = semnum, .sem_op = delta, .sem_flg = 0 };
    for (;;) {
        if (semop(semid, &op, 1) == 0) return;
        if (errno == EINTR) continue;
        perror("semop");
        exit(1);
    }
}

static void P(int semid, unsigned short semnum) { sem_op_retry(semid, semnum, -1); }
static void Vn(int semid, unsigned short semnum, int n) { sem_op_retry(semid, semnum, (short)n); }

static void worker_loop(int semid, Shared* sh, int is_wrench, const char* name) {
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    setbuf(stdout, NULL);

    int slot_sem = is_wrench ? SEM_NUT_SLOTS : SEM_SCREW_SLOTS;

    for (;;) {
        if (sh->stop) break;

        // "взять элемент" + ждать возможности установки
        P(semid, (unsigned short)slot_sem);

        // После пробуждения проверим stop и корректно вернём слот, если пора завершаться
        P(semid, SEM_MUTEX);
        if (sh->stop) {
            Vn(semid, SEM_MUTEX, 1);
            Vn(semid, (unsigned short)slot_sem, 1);
            break;
        }
        int dev = sh->device_id;
        Vn(semid, SEM_MUTEX, 1);

        // "установить" (имитация работы вне критической секции)
        usleep(50000 + (rand() % 150000));

        // Фиксируем результат установки
        P(semid, SEM_MUTEX);

        // dev не обязан использоваться, но можно печатать для наглядности
        if (is_wrench) sh->nuts++;
        else sh->screw++;

        if (sh->nuts > 2 || sh->screw > 1) {
            fprintf(stderr, "[%d] ОШИБКА: превышение лимитов (nuts=%d, screw=%d)\n",
                getpid(), sh->nuts, sh->screw);
            sh->stop = 1;
            Vn(semid, SEM_MUTEX, 1);
            break;
        }

        printf("[dev %d] %s поставил %s -> гайки=%d, винт=%d\n",
            dev, name, is_wrench ? "гайку" : "винт", sh->nuts, sh->screw);

        // Если всё установлено — заменить полуфабрикат (ровно один раз)
        if (sh->nuts == 2 && sh->screw == 1) {
            sh->completed++;
            printf("=== ГОТОВО: устройство #%d (итого %d/%d) ===\n",
                sh->device_id, sh->completed, sh->target);

            // "вынуть готовое и закрепить новый полуфабрикат"
            sh->device_id++;
            sh->nuts = 0;
            sh->screw = 0;

            // открыть слоты на новый полуфабрикат
            Vn(semid, SEM_NUT_SLOTS, 2);
            Vn(semid, SEM_SCREW_SLOTS, 1);

            if (sh->completed >= sh->target) {
                sh->stop = 1;
            }
        }

        Vn(semid, SEM_MUTEX, 1);
    }

    printf("[%d] %s завершил работу\n", getpid(), name);
}

int main(int argc, char** argv) {
    int target = 5;
    if (argc >= 2) {
        target = atoi(argv[1]);
        if (target <= 0) target = 5;
    }

    // Семофоры
    int semid = semget(IPC_PRIVATE, SEM_COUNT, IPC_CREAT | 0600);
    if (semid == -1) { perror("semget"); return 1; }

    unsigned short init_vals[SEM_COUNT];
    init_vals[SEM_MUTEX] = 1;
    init_vals[SEM_NUT_SLOTS] = 2;
    init_vals[SEM_SCREW_SLOTS] = 1;

    union semun arg;
    arg.array = init_vals;
    if (semctl(semid, 0, SETALL, arg) == -1) { perror("semctl SETALL"); return 1; }

    // Общая память
    int shmid = shmget(IPC_PRIVATE, (int)sizeof(Shared), IPC_CREAT | 0600);
    if (shmid == -1) { perror("shmget"); return 1; }

    Shared* sh = (Shared*)shmat(shmid, NULL, 0);
    if (sh == (void*)-1) { perror("shmat"); return 1; }

    memset(sh, 0, sizeof(*sh));
    sh->device_id = 1;
    sh->target = target;

    pid_t p[3];

    // 2 гаечных ключа
    for (int i = 0; i < 2; i++) {
        p[i] = fork();
        if (p[i] == 0) {
            Shared* child_sh = (Shared*)shmat(shmid, NULL, 0);
            if (child_sh == (void*)-1) { perror("child shmat"); exit(1); }
            char name[64];
            snprintf(name, sizeof(name), "Рабочий-ключ-%d", i + 1);
            worker_loop(semid, child_sh, 1, name);
            _exit(0);
        }
        if (p[i] < 0) { perror("fork"); return 1; }
    }

    // 1 отвертка
    p[2] = fork();
    if (p[2] == 0) {
        Shared* child_sh = (Shared*)shmat(shmid, NULL, 0);
        if (child_sh == (void*)-1) { perror("child shmat"); exit(1); }
        worker_loop(semid, child_sh, 0, "Рабочий-отвертка");
        _exit(0);
    }
    if (p[2] < 0) { perror("fork"); return 1; }

    // Ждём детей
    for (int i = 0; i < 3; i++) {
        int st = 0;
        waitpid(p[i], &st, 0);
    }

    // Очистка ресурсов
    shmdt(sh);
    shmctl(shmid, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID);

    return 0;
}   
