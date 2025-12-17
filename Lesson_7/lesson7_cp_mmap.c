#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static void die(const char* where)
{
    perror(where);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
    const char* src_path, * dst_path;
    int fd_src, fd_dst;
    struct stat st;
    size_t len;
    void* src_map, * dst_map;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <from> <to>\n", argv[0]);
        return EXIT_FAILURE;
    }

    src_path = argv[1];
    dst_path = argv[2];

    fd_src = open(src_path, O_RDONLY);
    if (fd_src < 0)
        die("open(src)");

    if (fstat(fd_src, &st) != 0) {
        close(fd_src);
        die("fstat(src)");
    }

    fd_dst = open(dst_path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd_dst < 0) {
        close(fd_src);
        die("open(dst)");
    }

    if (ftruncate(fd_dst, st.st_size) != 0) {
        close(fd_src);
        close(fd_dst);
        die("ftruncate(dst)");
    }

    if (st.st_size == 0) {
        if (fsync(fd_dst) != 0) {
            close(fd_src);
            close(fd_dst);
            die("fsync(dst)");
        }
        if (close(fd_src) != 0)
            die("close(src)");
        if (close(fd_dst) != 0)
            die("close(dst)");
        return EXIT_SUCCESS;
    }

    len = (size_t)st.st_size;

    src_map = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd_src, 0);
    if (src_map == MAP_FAILED) {
        close(fd_src);
        close(fd_dst);
        die("mmap(src)");
    }

    dst_map = mmap(NULL, len, PROT_WRITE, MAP_SHARED, fd_dst, 0);
    if (dst_map == MAP_FAILED) {
        munmap(src_map, len);
        close(fd_src);
        close(fd_dst);
        die("mmap(dst)");
    }

    memcpy(dst_map, src_map, len);

    if (msync(dst_map, len, MS_SYNC) != 0) {
        munmap(src_map, len);
        munmap(dst_map, len);
        close(fd_src);
        close(fd_dst);
        die("msync(dst)");
    }

    if (munmap(src_map, len) != 0) {
        munmap(dst_map, len);
        close(fd_src);
        close(fd_dst);
        die("munmap(src)");
    }
    if (munmap(dst_map, len) != 0) {
        close(fd_src);
        close(fd_dst);
        die("munmap(dst)");
    }

    if (close(fd_src) != 0)
        die("close(src)");
    if (fsync(fd_dst) != 0) {
        close(fd_dst);
        die("fsync(dst)");
    }
    if (close(fd_dst) != 0)
        die("close(dst)");

    return EXIT_SUCCESS;
}