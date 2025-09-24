#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void) {
    const int N = 3;
    pid_t pid;
    printf("Process 0 (root parent) PID: %d\n", getpid());
    fflush(stdout);
    for (int i = 1; i <= N; i++) {
        pid = fork();
        if (pid < 0) {
            perror("fork failed");
            exit(1);
        }
        else if (pid == 0) {
            printf("Process %d created with PID %d (parent %d)\n", i, getpid(), getppid());
            fflush(stdout);
            if (i < N) {
                continue;
            }
            else {
                sleep(2);
                exit(0);
            }
        }
        else {
            wait(NULL);
            exit(0);
        }
    }
    return 0;
}
