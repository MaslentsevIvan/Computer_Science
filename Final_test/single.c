#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define SHM_NAME "/single_shm_example"
#define SEM_NAME "/single_sem_example"

/*
 * Разделяемое состояние между всеми процессами single.
 *
 * active — количество процессов, которые сейчас находятся в стадии печати строки посимвольно.
 */
struct shared_state {
    int active;
};

/* Флаг, который выставляется обработчиком SIGINT (Ctrl-C). */
static volatile sig_atomic_t stop_flag = 0;

/* Глобальные указатели/дескрипторы для аккуратной очистки. */
static struct shared_state* g_state = NULL;
static sem_t* g_sem = NULL;
static int g_registered = 0; /* увеличивали ли active для этого процесса */

/*
 * Обработчик SIGINT:
 *
 *   по Ctrl-C мы не завершаем процесс мгновенно, а только ставим флажок stop_flag.
 *
 *   Основной поток, увидев stop_flag, аккуратно выйдет из цикла печати, а затем уменьшит счётчик active.
 */
static void
sigint_handler(int signo)
{
    (void)signo;
    stop_flag = 1;
}

/*
 * Вспомогательная функция: безопасно войти в критическую секцию семафора.
 *
 * sem_wait может вернуться с -1 и errno == EINTR, если вызов был прерван сигналом. Тогда повторяем попытку.
 */
static int
sem_wait_retry(sem_t* sem)
{
    for (;;) {
        if (sem_wait(sem) == 0) {
            return 0;
        }
        if (errno == EINTR) {
            /* Прервали сигналом — пробуем ещё раз. */
            continue;
        }
        return -1;
    }
}

/*
 * Основной алгоритм программы:
 *
 *   ┌ 1) Установить обработчик SIGINT.
 *   │
 *   ├ 2) Открыть/создать именованный семафор и разделяемую память:
 *   │      sem = sem_open(...)
 *   │      shm_fd = shm_open(...)
 *   │      ftruncate(shm_fd, sizeof(struct shared_state))
 *   │      state = mmap(...)
 *   │
 *   ├ 3) КРИТИЧЕСКАЯ СЕКЦИЯ:
 *   │      sem_wait(sem)
 *   │        ┌ if (state->active == 0)
 *   │        │      msg = "Hello world";
 *   │        └ else
 *   │               msg = "Goodbye world";
 *   │        state->active++;
 *   │        g_registered = 1;
 *   │      sem_post(sem)
 *   │
 *   ├ 4) Печатать msg по одному символу в секунду:
 *   │      for c in msg:
 *   │          write(1, &c, 1)
 *   │          sleep(1)
 *   │          если stop_flag == 1 → выходим раньше конца строки
 *   │
 *   └ 5) КРИТИЧЕСКАЯ СЕКЦИЯ ОЧИСТКИ:
 *          sem_wait(sem)
 *              если g_registered и state->active > 0:
 *                   state->active--
 *          sem_post(sem)
 *          закрыть mmap, shm_fd, sem
 */
int main(void)
{
    const char* hello = "Hello world\n";
    const char* goodbye = "Goodbye world\n";
    const char* msg = NULL;

    int shm_fd = -1;

    /* ---------- 1. Устанавливаем обработчик SIGINT ---------- */

    /*
     /* Используем sigaction, чтобы:
     *   - задать функцию-обработчик sigint_handler,
     *   - включить SA_RESTART (где это возможно).
     */
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    /* ---------- 2. Открываем/создаём семафор и разделяемую память ---------- */

    /*
     * POSIX именованный семафор:
     *
     * sem_open(SEM_NAME, O_CREAT, 0666, 1)
     *
     *   - имя SEM_NAME разделяется между всеми процессами;
     *   - последний аргумент (1) — начальное значение семафора.
     */
    g_sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (g_sem == SEM_FAILED) {
        perror("sem_open");
        return EXIT_FAILURE;
    }

    /*
     * POSIX разделяемая память:
     *
     *   shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
     *   ftruncate(shm_fd, sizeof(struct shared_state));
     *   g_state = mmap(..., MAP_SHARED, shm_fd, 0);
     *
     * Все процессы с одним и тем же именем SHM_NAME будут видеть один и тот же участок памяти.
     */
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        sem_close(g_sem);
        return EXIT_FAILURE;
    }

    /* Задаём (или подтверждаем) размер объекта под одну структуру. */
    if (ftruncate(shm_fd, (off_t)sizeof(struct shared_state)) == -1) {
        perror("ftruncate");
        close(shm_fd);
        sem_close(g_sem);
        return EXIT_FAILURE;
    }

    /* Отображаем объект разделяемой памяти в адресное пространство процесса. */
    g_state = mmap(NULL,
        sizeof(struct shared_state),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        shm_fd,
        0);
    if (g_state == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        sem_close(g_sem);
        return EXIT_FAILURE;
    }

     /* ---------- 3. Заходим в критическую секцию: выбор строки и инкремент active ---------- */

    if (sem_wait_retry(g_sem) == -1) {
        perror("sem_wait");
        goto cleanup;
    }

    /*
     * КРИТИЧЕСКАЯ СЕКЦИЯ:
     *
     *   if (g_state->active == 0) {
     *       // мы первый/единственный процесс, печатающий строку
     *       msg = "Hello world\n";
     *   } else {
     *       // в системе уже кто-то печатает
     *       msg = "Goodbye world\n";
     *   }
     *
     *   g_state->active++;
     *   g_registered = 1;   // помечаем, что мы вошли в "счётчик"
     *
     * Выход из критической секции:
     *   sem_post(g_sem);
     */
    if (g_state->active == 0) {
        msg = hello;
    }
    else {
        msg = goodbye;
    }

    g_state->active++;
    g_registered = 1;

    if (sem_post(g_sem) == -1) {
        perror("sem_post");
        goto cleanup;
    }

    /* ---------- 4. Печать строки по одному символу в секунду ---------- */

    /*
     * Алгоритм вывода:
     *
     *   len = strlen(msg)
     *   for i = 0..len-1:
     *       write(1, &msg[i], 1)
     *       если stop_flag == 1:
     *           break
     *       если это не последний символ:
     *           sleep(1)
     *
     * Используем низкоуровневый вывод write(2), а не stdio,
     * чтобы подчеркнуть работу с файловыми дескрипторами.
     *
     * Темы курса:
     *   - Низкоуровневый ввод-вывод: write(2).
     *   - Обработка EINTR (при сигнале) — повторяем запись того же символа.
     */
    {
        size_t len = strlen(msg);
        size_t i;

        for (i = 0; i < len; i++) {
            ssize_t w;

            /* Пытаемся вывести один символ. */
            w = write(STDOUT_FILENO, &msg[i], 1);
            if (w == -1) {
                if (errno == EINTR) {
                    /*
                     * Системный вызов write прерван сигналом.
                     * Повторим попытку вывести тот же символ:
                     *
                     *   ┌── i--;        // откатываем индекс
                     *   └── continue;   // следующая итерация снова попытается этот же символ
                     */
                    i--;
                    continue;
                }

                perror("write");
                break;
            }

            /*
             * Если пользователь нажал Ctrl-C, stop_flag станет 1.
             * После печати текущего символа выходим из цикла,
             * чтобы аккуратно уменьшить active.
             */
            if (stop_flag) {
                break;
            }

            /* Делаем паузу 1 секунда между символами, кроме последнего. */
            if (i + 1 < len) {
                sleep(1);
            }

            if (stop_flag) {
                break;
            }
        }

        /*
         * Если нас прервали по Ctrl-C посреди строки, мы могли не вывести '\n'.
         * Чтобы приглашение шелла не прилипало к последней букве, печатаем перевод строки.
         */
        if (stop_flag) {
            const char nl = '\n';
            if (write(STDOUT_FILENO, &nl, 1) == -1) {
                /* Ошибку здесь можно игнорировать. */
            }
        }
    }

    /* ---------- 5. Очистка: уменьшаем active и закрываем ресурсы ---------- */

cleanup:
    /*
     * Нам важно не оставить систему в состоянии, будто мы всё ещё печатаем.
     *
     * Выход из стадии вывода:
     *
     *   если (g_registered):
     *       sem_wait(g_sem)
     *           если active > 0:
     *               active--
     *       sem_post(g_sem)
     */
    if (g_sem != NULL && g_sem != SEM_FAILED &&
        g_state != NULL && g_state != MAP_FAILED &&
        g_registered) {

        if (sem_wait_retry(g_sem) == -1) {
            perror("sem_wait (cleanup)");
        }
        else {
            if (g_state->active > 0) {
                g_state->active--;
            }

            if (sem_post(g_sem) == -1) {
                perror("sem_post (cleanup)");
            }
        }
    }

    /* Отсоединяем разделяемую память. */
    if (g_state != NULL && g_state != MAP_FAILED) {
        if (munmap(g_state, sizeof(struct shared_state)) == -1) {
            perror("munmap");
        }
    }

    /* Закрываем файловый дескриптор shm. */
    if (shm_fd != -1) {
        if (close(shm_fd) == -1) {
            perror("close shm_fd");
        }
    }

    /* Закрываем семафор. */
    if (g_sem != NULL && g_sem != SEM_FAILED) {
        if (sem_close(g_sem) == -1) {
            perror("sem_close");
        }
    }

    return EXIT_SUCCESS;
}
