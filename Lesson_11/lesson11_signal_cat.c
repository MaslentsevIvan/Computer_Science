#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const int BITS = 8;

static void die(const char* msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void x_sigprocmask(int how, const sigset_t* set) {
    if (sigprocmask(how, set, NULL) == -1) die("sigprocmask");
}

static void x_sigqueue(pid_t pid, int sig, int val) {
    union sigval sv;
    sv.sival_int = val;
    if (sigqueue(pid, sig, sv) == -1) die("sigqueue");
}

static int x_sigwaitinfo(const sigset_t* mask, siginfo_t* info) {
    for (;;) {
        int s = sigwaitinfo(mask, info);
        if (s >= 0) return s;
        if (errno == EINTR) continue;
        die("sigwaitinfo");
    }
}

static void parent_send(pid_t child, pid_t self,
    const unsigned char* msg, size_t len,
    const sigset_t* mask,
    int sig_bit, int sig_ack, int sig_fin) {
    puts("Source data (little endian bits):");
    puts("0 1 2 3 4 5 6 7");
    fflush(stdout);

    for (size_t i = 0; i < len; ++i) {
        unsigned char c = msg[i];
        for (int b = 0; b < BITS; ++b) {
            int bit = (c >> b) & 1;
            printf("%d ", bit);
            fflush(stdout);

            // отправляем один "датасигнал" с payload=0/1
            x_sigqueue(child, sig_bit, bit);

            // ждём ACK строго от ребёнка
            for (;;) {
                siginfo_t info;
                int got = x_sigwaitinfo(mask, &info);
                if (got == sig_ack && info.si_pid == child) break;
                // игнорируем посторонние/неожиданные сигналы
            }
        }
        printf("- '%c'\n", (c >= 32 && c < 127) ? c : '.');
        fflush(stdout);
    }

    // признак конца передачи: payload=2
    x_sigqueue(child, sig_fin, 2);

    // (не обязательно) дождаться последнего ACK на fin
    for (;;) {
        siginfo_t info;
        int got = x_sigwaitinfo(mask, &info);
        if (got == sig_ack && info.si_pid == child) break;
    }

    (void)self;
}

static void child_recv(pid_t parent, size_t len,
    const sigset_t* mask,
    int sig_bit, int sig_ack, int sig_fin) {
    unsigned char* buf = malloc(len + 1);
    if (!buf) die("malloc");
    memset(buf, 0, len + 1);

    // принимаем ровно len байт, затем fin
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = 0;
        for (int b = 0; b < BITS; ++b) {
            siginfo_t info;
            int got;

            // ждём "бит" только от родителя
            for (;;) {
                got = x_sigwaitinfo(mask, &info);
                if (info.si_pid != parent) continue;
                if (got == sig_bit || got == sig_fin) break;
            }

            if (got == sig_fin && info.si_value.sival_int == 2) {
                // fin пришёл раньше, не зависаем
                buf[i] = c;
                goto done;
            }

            int bit = info.si_value.sival_int & 1;
            c |= (unsigned char)(bit << b);

            // ACK родителю
            x_sigqueue(parent, sig_ack, 0);
        }
        buf[i] = c;
    }

    // ждём fin после всех байт (чтобы не было гонок завершения)
    for (;;) {
        siginfo_t info;
        int got = x_sigwaitinfo(mask, &info);
        if (info.si_pid == parent && got == sig_fin && info.si_value.sival_int == 2) {
            x_sigqueue(parent, sig_ack, 0); // финальный ACK
            break;
        }
    }

done:
    puts("Received data:");
    buf[len] = '\0';
    printf("%s\n", (char*)buf);
    fflush(stdout);
    free(buf);
    exit(EXIT_SUCCESS);
}

int main(int argc, char** argv) {
    const char* s = (argc >= 2) ? argv[1] : "Hello World!\n";
    size_t len = strlen(s);

    // Выбираем реальные номера RT-сигналов в рантайме (SIGRTMIN не const!)
    int rtmin = SIGRTMIN;
    int rtmax = SIGRTMAX;
    if (rtmin + 3 > rtmax) {
        fprintf(stderr, "Not enough real-time signals: SIGRTMIN..SIGRTMAX too small\n");
        return EXIT_FAILURE;
    }

    int sig_bit = rtmin + 0;  // data, payload 0/1
    int sig_ack = rtmin + 1;  // ack
    int sig_fin = rtmin + 2;  // fin, payload 2
    // rtmin+3 оставили в запасе

    // Блокируем наши сигналы ДО fork(), чтобы sigwaitinfo работал корректно
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, sig_bit);
    sigaddset(&mask, sig_ack);
    sigaddset(&mask, sig_fin);
    x_sigprocmask(SIG_BLOCK, &mask);

    pid_t parent_pid = getpid();
    pid_t pid = fork();
    if (pid < 0) die("fork");

    if (pid == 0) {
        child_recv(parent_pid, len, &mask, sig_bit, sig_ack, sig_fin);
    }
    else {
        parent_send(pid, parent_pid, (const unsigned char*)s, len, &mask, sig_bit, sig_ack, sig_fin);
        int st = 0;
        if (waitpid(pid, &st, 0) == -1) die("waitpid");
        return WIFEXITED(st) ? WEXITSTATUS(st) : EXIT_FAILURE;
    }
}
