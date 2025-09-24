#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(int argc, char* argv[]) {
    int n = argc - 1;
    int* numbers = malloc(n * sizeof(int));
    for (int i = 0; i < n; i++) {
        numbers[i] = atoi(argv[i + 1]);
    }
    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            free(numbers);
            return 1;
        }
        if (pid == 0) {
            sleep(numbers[i]);
            printf("%d ", numbers[i]);
            fflush(stdout);
            exit(0);
        }
    }
    for (int i = 0; i < n; i++) {
        wait(NULL);
    }
    free(numbers);
    printf("\n");
    return 0;
}
