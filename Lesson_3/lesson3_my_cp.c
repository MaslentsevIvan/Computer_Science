#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <getopt.h>

#define BUF_SIZE 65536

static int opt_f = 0;  // --force
static int opt_i = 0;  // --interactive
static int opt_v = 0;  // --verbose

static void usage(const char* prog) {
    fprintf(stderr, "Usage:\n  %s [-fiv] SRC DEST\n  %s [-fiv] SRC... DIR\n", prog, prog);
    exit(1);
}

static int confirm_overwrite(const char* dst) {
    fprintf(stderr, "overwrite '%s'? [y/N] ", dst);
    int c = getchar();
    if (c != '\n') {
        int d;
        while ((d = getchar()) != '\n' && d != EOF) {}
    }
    return (c == 'y' || c == 'Y');
}

static void join_path(char* out, size_t outsz, const char* dir, const char* name) {
    size_t dl = strlen(dir);
    if (dl > 0 && dir[dl - 1] == '/')
        snprintf(out, outsz, "%s%s", dir, name);
    else
        snprintf(out, outsz, "%s/%s", dir, name);
}

static const char* filename_of(const char* path, char* scratch, size_t cap) {
    strncpy(scratch, path, cap - 1);
    scratch[cap - 1] = '\0';
    return basename(scratch);
}

static int copy_file(const char* src, const char* dst) {
    int in_fd = open(src, O_RDONLY);
    if (in_fd < 0) { perror(src); return -1; }

    // Запрет копирования директории без -r (поведение как у GNU cp)
    struct stat st_src;
    if (fstat(in_fd, &st_src) == 0 && S_ISDIR(st_src.st_mode)) {
        fprintf(stderr, "cp: -r not specified; omitting directory '%s'\n", src);
        close(in_fd);
        return -1;
    }

    // Защита: SRC и DEST — один и тот же файл?
    struct stat st_dst;
    if (stat(dst, &st_dst) == 0) {
        if (S_ISREG(st_src.st_mode) && S_ISREG(st_dst.st_mode) &&
            st_src.st_dev == st_dst.st_dev && st_src.st_ino == st_dst.st_ino) {
            fprintf(stderr, "cp: '%s' and '%s' are the same file\n", src, dst);
            close(in_fd);
            return -1;
        }
        // Если включён -i — спросим подтверждение
        if (opt_i && !confirm_overwrite(dst)) {
            if (opt_v) fprintf(stdout, "skipped '%s'\n", dst);
            close(in_fd);
            return 0;
        }
    }

    int out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out_fd < 0) {
        // Если не удалось и у нас -f, попробуем удалить и создать заново
        if (opt_f && errno != ENOENT) {
            (void)unlink(dst);
            out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        }
        if (out_fd < 0) { perror(dst); close(in_fd); return -1; }
    }

    char buf[BUF_SIZE];
    ssize_t n;
    while ((n = read(in_fd, buf, sizeof buf)) > 0) {
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(out_fd, buf + off, n - off);
            if (w < 0) { perror("write"); close(in_fd); close(out_fd); return -1; }
            off += w;
        }
    }
    if (n < 0) { perror("read"); close(in_fd); close(out_fd); return -1; }

    if (close(out_fd) < 0) { perror("close"); close(in_fd); return -1; }
    close(in_fd);

    if (opt_v) printf("'%s' -> '%s'\n", src, dst);
    return 0;
}

int main(int argc, char* argv[]) {
    static struct option long_opts[] = {
        {"force",       no_argument, 0, 'f'},
        {"interactive", no_argument, 0, 'i'},
        {"verbose",     no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };

    int ch;
    while ((ch = getopt_long(argc, argv, "fiv", long_opts, NULL)) != -1) {
        switch (ch) {
        case 'f': opt_f = 1; break;
        case 'i': opt_i = 1; break;
        case 'v': opt_v = 1; break;
        default: usage(argv[0]);
        }
    }

    int n_args = argc - optind;
    if (n_args < 2) usage(argv[0]);

    const char* dest = argv[argc - 1];
    struct stat st;
    int dest_is_dir = (stat(dest, &st) == 0 && S_ISDIR(st.st_mode));

    if (n_args == 2 && !dest_is_dir) {
        // SRC -> DEST
        return copy_file(argv[optind], dest) ? 1 : 0;
    }

    if (!dest_is_dir) {
        fprintf(stderr, "target '%s' is not a directory\n", dest);
        return 1;
    }

    // SRC... -> DIR
    for (int i = optind; i < argc - 1; i++) {
        char scratch[PATH_MAX];
        const char* base = filename_of(argv[i], scratch, sizeof scratch);
        char path[PATH_MAX];
        join_path(path, sizeof path, dest, base);
        if (copy_file(argv[i], path) != 0) return 1;
    }

    return 0;
}