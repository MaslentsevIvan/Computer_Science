#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void) {
    const int N = 3;
    pid_t pid;
    printf("Parent PID: %d\n", getpid());
    for (int i = 0; i < N; i++) {
        pid = fork();
        if (pid < 0) {
            perror("fork failed");
            exit(1);
        }
        else if (pid == 0) {
            printf("Child %d created with PID %d (parent %d)\n",i, getpid(), getppid());
            sleep(2);
            exit(0);
        }
    }
    for (int i = 0; i < N; i++) {
        wait(NULL);
    }
    printf("Parent %d: all children finished.\n", getpid());
    return 0;
}