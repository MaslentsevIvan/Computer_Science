#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#define BUF_SIZE 65536

int copy_fd(int fd) {
    char buf[BUF_SIZE];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        if (write(STDOUT_FILENO, buf, n) != n) {
            perror("write");
            return 1;
        }
    }
    if (n < 0) { perror("read"); return 1; }
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 1) return copy_fd(STDIN_FILENO);

    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            status |= copy_fd(STDIN_FILENO);
            continue;
        }
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) { perror(argv[i]); status = 1; continue; }
        status |= copy_fd(fd);
        close(fd);
    }
    return status;
}