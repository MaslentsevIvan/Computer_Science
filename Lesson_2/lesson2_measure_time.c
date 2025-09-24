#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>

int main(int argc, char* argv[]) {
    struct timeval start, end;
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return 1;
    }
    else if (pid == 0) {
        execvp(argv[1], &argv[1]);
        perror("exec failed");
        exit(1);
    }
    else {
        gettimeofday(&start, NULL);
        int status;
        waitpid(pid, &status, 0);
        gettimeofday(&end, NULL);
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
        printf("Execution time: %.6f seconds\n", elapsed);
    }
    return 0;
}