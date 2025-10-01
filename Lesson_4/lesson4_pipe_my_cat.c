#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define BUF_SIZE 4096

static int write_all(int fd, const char* buf, ssize_t n) {
    ssize_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, buf + off, (size_t)(n - off));
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += w;
    }
    return 0;
}

int main(int argc, char** argv) {
    int in = STDIN_FILENO;
    if (argc > 1 && strcmp(argv[1], "-") != 0) {
        in = open(argv[1], O_RDONLY);
        if (in < 0) { perror("open"); return 1; }
    }

    int pfd[2];
    if (pipe(pfd) < 0) { perror("pipe"); return 1; }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {

        close(pfd[1]);
        char buf[BUF_SIZE];
        for (;;) {
            ssize_t n = read(pfd[0], buf, sizeof buf);
            if (n == 0) break;
            if (n < 0) {
                if (errno == EINTR) continue;
                perror("read(pipe)");
                return 1;
            }
            if (write_all(STDOUT_FILENO, buf, n) < 0) {
                perror("write(stdout)");
                return 1;
            }
        }
        close(pfd[0]);
        return 0;
    }

    close(pfd[0]);
    char buf[BUF_SIZE];
    for (;;) {
        ssize_t n = read(in, buf, sizeof buf);
        if (n == 0) break;
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read(input)");
            break;
        }
        if (write_all(pfd[1], buf, n) < 0) {
            perror("write(pipe)");
            break;
        }
    }
    close(pfd[1]);
    if (in != STDIN_FILENO) close(in);
    return 0;
}