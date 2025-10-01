#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

typedef struct {
    unsigned long long bytes;
    unsigned long long words;
    unsigned long long lines;
} Counts;

static void count_fd(int fd, Counts* c)
{
    char buf[8192];
    ssize_t n;
    int in_word = 0;

    while ((n = read(fd, buf, sizeof buf)) > 0) {
        c->bytes += (unsigned long long)n;
        for (ssize_t i = 0; i < n; ++i) {
            unsigned char ch = (unsigned char)buf[i];
            if (ch == '\n') c->lines++;
            if (isspace(ch)) {
                in_word = 0;
            }
            else if (!in_word) {
                c->words++;
                in_word = 1;
            }
        }
    }
    if (n < 0) {
        perror("read");
        exit(1);
    }
}

int main(int argc, char* argv[])
{
    Counts c = { 0, 0, 0 };

    if (argc == 1) {
        count_fd(STDIN_FILENO, &c);
        printf("%llu %llu %llu\n",
            (unsigned long long)c.bytes,
            (unsigned long long)c.words,
            (unsigned long long)c.lines);
        return 0;
    }

    int pfd[2];
    if (pipe(pfd) == -1) {
        perror("pipe");
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        if (close(pfd[0]) == -1) {
            perror("close");
            _exit(127);
        }
        // Перенаправить stdout -> pipe write end:
        if (dup2(pfd[1], STDOUT_FILENO) == -1) {
            perror("dup2");
            _exit(127);
        }
        // write end больше не нужен как отдельный дескриптор
        if (close(pfd[1]) == -1) {
            perror("close");
            _exit(127);
        }
        // Запускаем команду: argv[1] ... argv[argc-1]
        execvp(argv[1], &argv[1]);
        // Если тут оказались — exec не удался
        perror("execvp");
        _exit(127);
    }
    else {
        if (close(pfd[1]) == -1) {
            perror("close");
            return 1;
        }

        count_fd(pfd[0], &c);

        if (close(pfd[0]) == -1) {
            perror("close");
            return 1;
        }

        int status = 0;
        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid");
            return 1;
        }
        // Если команда упала, вернём её код (по желанию — закомментируйте).
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            // напечатаем статистику всё равно:
            printf("%llu %llu %llu\n",
                (unsigned long long)c.bytes,
                (unsigned long long)c.words,
                (unsigned long long)c.lines);
            return WEXITSTATUS(status);
        }

        printf("%llu %llu %llu\n",
            (unsigned long long)c.bytes,
            (unsigned long long)c.words,
            (unsigned long long)c.lines);
        return 0;
    }
}