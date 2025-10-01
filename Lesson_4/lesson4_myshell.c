#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* ===== Utils ===== */

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static char *trim(char *s) {
    if (!s) return s;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) *e-- = '\0';
    return s;
}

static char *expand_tilde(const char *arg) {
    if (!arg) return NULL;
    if (arg[0] != '~') return strdup(arg);
    if (arg[1] != '\0' && arg[1] != '/') {
        return strdup(arg);
    }
    const char *home = getenv("HOME");
    if (!home || !*home) home = "/";
    const char *rest = (arg[1] == '/') ? (arg + 1) : "";
    size_t len = strlen(home) + strlen(rest) + 1;
    char *out = malloc(len);
    if (!out) die("malloc");
    strcpy(out, home);
    strcat(out, rest);
    return out;
}

static int is_builtin(const char *cmd) {
    return cmd &&
           (strcmp(cmd, "exit") == 0 ||
            strcmp(cmd, "cd")   == 0 ||
            strcmp(cmd, "pwd")  == 0);
}

/* ===== Parse ===== */

static char **split_segments(char *line, char delim, size_t *out_n) {
    size_t cap = 8, n = 0;
    char **arr = malloc(cap * sizeof(*arr));
    if (!arr) die("malloc");

    char *p = line;
    while (1) {
        char *seg_start = p;
        bool in_s = false, in_d = false;
        for (; *p; ++p) {
            if (!in_s && *p == '"') { in_d = !in_d; continue; }
            if (!in_d && *p == '\''){ in_s = !in_s; continue; }
            if (!in_s && !in_d && *p == delim) break;
        }
        if (*p == delim) { *p = '\0'; p++; }
        char *clean = trim(seg_start);
        if (*clean) {
            if (n == cap) { cap *= 2; arr = realloc(arr, cap * sizeof(*arr)); if (!arr) die("realloc"); }
            arr[n++] = clean;
        }
        if (!*p) break;
    }
    *out_n = n;
    return arr;
}

static char **parse_argv(char *segment) {
    size_t cap = 8, argc = 0;
    char **argv = malloc(cap * sizeof(*argv));
    if (!argv) die("malloc");

    char *p = segment;
    while (*p) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;

        bool in_s = false, in_d = false;
        char *start = p, *w = p;
        while (*p) {
            char c = *p++;
            if (c == '\'' && !in_d) { in_s = !in_s; continue; }
            if (c == '"'  && !in_s) { in_d = !in_d; continue; }
            if (!in_s && !in_d && isspace((unsigned char)c)) { break; }
            *w++ = c;
        }
        *w = '\0';
        if (argc == cap) { cap *= 2; argv = realloc(argv, cap * sizeof(*argv)); if (!argv) die("realloc"); }
        argv[argc++] = start;
    }
    argv[argc] = NULL;
    return argv;
}

static void free_argvs(char ***argvs, size_t n) {
    if (!argvs) return;
    for (size_t i = 0; i < n; ++i) free(argvs[i]);
    free(argvs);
}

static int extract_redirs(char **argv, char **in_path, char **out_path, int *append) {
    *in_path = NULL; *out_path = NULL; *append = 0;
    if (!argv || !argv[0]) return 0;

    size_t i = 0, j = 0;
    while (argv[i]) {
        if (strcmp(argv[i], "<") == 0) {
            if (!argv[i+1]) return -1;
            *in_path = argv[i+1];
            i += 2; continue;
        }
        if (strcmp(argv[i], ">") == 0 || strcmp(argv[i], ">>") == 0) {
            if (!argv[i+1]) return -1;
            *out_path = argv[i+1];
            *append   = (argv[i][1] == '>');
            i += 2; continue;
        }
        argv[j++] = argv[i++];
    }
    argv[j] = NULL;
    return 0;
}

/* ===== Pipeline ===== */

static int run_pipeline(char ***argvs, size_t nsegs) {
    if (nsegs == 0) return 0;

    if (nsegs == 1 && argvs[0] && argvs[0][0] && is_builtin(argvs[0][0])) {
        if (strcmp(argvs[0][0], "exit") == 0) {
            int code = argvs[0][1] ? atoi(argvs[0][1]) : 0;
            exit(code);
        }
        if (strcmp(argvs[0][0], "cd") == 0) {
            const char *raw = argvs[0][1];
            char *path = NULL;
            if (!raw) {
                const char *home = getenv("HOME");
                path = strdup(home && *home ? home : "/");
            } else if (raw[0] == '~') {
                path = expand_tilde(raw);
            } else {
                path = strdup(raw);
            }
            if (!path) die("strdup");
            if (chdir(path) != 0) {
                fprintf(stderr, "myshell: cd: %s: %s\n", path, strerror(errno));
                free(path);
                return 1;
            }
            free(path);
            return 0;
        }
        if (strcmp(argvs[0][0], "pwd") == 0) {
            char buf[4096];
            if (!getcwd(buf, sizeof(buf))) { perror("pwd"); return 1; }
            puts(buf);
            return 0;
        }
    }

    int debug = getenv("MYSHELL_DEBUG") ? 1 : 0;

    size_t npipes = (nsegs > 1) ? (nsegs - 1) : 0;
    int (*pipes)[2] = NULL;
    if (npipes) {
        pipes = malloc(npipes * sizeof(int[2]));
        if (!pipes) die("malloc pipes");
        for (size_t i = 0; i < npipes; ++i) {
            if (pipe(pipes[i]) == -1) {
                perror("pipe");
                for (size_t k = 0; k < i; ++k) { close(pipes[k][0]); close(pipes[k][1]); }
                free(pipes);
                return -1;
            }
        }
    }

    pid_t *pids = malloc(nsegs * sizeof(pid_t));
    if (!pids) die("malloc pids");

    for (size_t i = 0; i < nsegs; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            for (size_t k = 0; k < npipes; ++k) { close(pipes[k][0]); close(pipes[k][1]); }
            for (size_t k = 0; k < i; ++k) waitpid(pids[k], NULL, 0);
            free(pipes);
            free(pids);
            return -1;
        }
        if (pid == 0) {
            struct sigaction dfl = {0};
            dfl.sa_handler = SIG_DFL;
            sigaction(SIGINT, &dfl, NULL);

            if (i > 0) {
                if (dup2(pipes[i-1][0], STDIN_FILENO) == -1) die("dup2 stdin");
            }
            if (i + 1 < nsegs) {
                if (dup2(pipes[i][1], STDOUT_FILENO) == -1) die("dup2 stdout");
            }

            char *in_path = NULL, *out_path = NULL;
            int append = 0;
            if (extract_redirs(argvs[i], &in_path, &out_path, &append) != 0) {
                fprintf(stderr, "myshell: redirection syntax error\n");
                _exit(2);
            }
            if (in_path) {
                int fd = open(in_path, O_RDONLY);
                if (fd < 0) { perror(in_path); _exit(1); }
                if (dup2(fd, STDIN_FILENO) == -1) { perror("dup2 <"); _exit(1); }
                close(fd);
            }
            if (out_path) {
                int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
                int fd = open(out_path, flags, 0666);
                if (fd < 0) { perror(out_path); _exit(1); }
                if (dup2(fd, STDOUT_FILENO) == -1) { perror("dup2 >"); _exit(1); }
                close(fd);
            }

            for (size_t k = 0; k < npipes; ++k) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }

            if (!argvs[i] || !argvs[i][0]) {
                fprintf(stderr, "myshell: empty command in pipeline\n");
                _exit(2);
            }

            if (debug) {
                fprintf(stderr, "exec:");
                for (char **a = argvs[i]; a && *a; ++a) fprintf(stderr, " [%s]", *a);
                fprintf(stderr, "\n");
            }

            if (is_builtin(argvs[i][0])) {
                if (strcmp(argvs[i][0], "exit") == 0) {
                    int code = argvs[i][1] ? atoi(argvs[i][1]) : 0;
                    _exit(code);
                } else if (strcmp(argvs[i][0], "cd") == 0) {
                    const char *raw = argvs[i][1];
                    char *path = NULL;
                    if (!raw) {
                        const char *home = getenv("HOME");
                        path = strdup(home && *home ? home : "/");
                    } else if (raw[0] == '~') {
                        path = expand_tilde(raw);
                    } else {
                        path = strdup(raw);
                    }
                    if (!path) die("strdup");
                    int rc = 0;
                    if (chdir(path) != 0) {
                        fprintf(stderr, "myshell: cd: %s: %s\n", path, strerror(errno));
                        rc = 1;
                    }
                    free(path);
                    _exit(rc);
                } else if (strcmp(argvs[i][0], "pwd") == 0) {
                    char buf[4096];
                    if (!getcwd(buf, sizeof(buf))) { perror("pwd"); _exit(1); }
                    puts(buf);
                    _exit(0);
                }
            }

            execvp(argvs[i][0], argvs[i]);
            fprintf(stderr, "%s: %s\n", argvs[i][0], strerror(errno));
            _exit(127);
        } else {
            pids[i] = pid;
        }
    }

    for (size_t k = 0; k < npipes; ++k) {
        close(pipes[k][0]);
        close(pipes[k][1]);
    }

    int rc = 0;
    for (size_t i = 0; i < nsegs; ++i) {
        int st = 0;
        if (waitpid(pids[i], &st, 0) == -1) {
            perror("waitpid");
            rc = -1;
        } else if (i == nsegs - 1) {
            if (WIFEXITED(st)) rc = WEXITSTATUS(st);
            else if (WIFSIGNALED(st)) rc = 128 + WTERMSIG(st);
        }
    }

    free(pids);
    free(pipes);
    return rc;
}

/* ===== Main loop ===== */

int main(void) {
    struct sigaction sa = {0};
    sa.sa_handler = SIG_IGN;
    sigaction(SIGINT, &sa, NULL);

    if (isatty(STDOUT_FILENO)) setvbuf(stdout, NULL, _IOLBF, 0);

    char *line = NULL;
    size_t cap = 0;

    while (1) {
        fputs("myshell$ ", stdout);
        fflush(stdout);

        ssize_t nread = getline(&line, &cap, stdin);
        if (nread < 0) { fputc('\n', stdout); break; }
        if (nread > 0 && line[nread-1] == '\n') line[nread-1] = '\0';

        char *ln = trim(line);
        if (*ln == '\0') continue;

        size_t nsegs = 0;
        char **segments = split_segments(ln, '|', &nsegs);
        if (nsegs == 0) { free(segments); continue; }

        char ***argvs = malloc(nsegs * sizeof(*argvs));
        if (!argvs) die("malloc argvs");
        for (size_t i = 0; i < nsegs; ++i) {
            argvs[i] = parse_argv(segments[i]);
            if (!argvs[i] || !argvs[i][0]) {
            }
        }

        (void)run_pipeline(argvs, nsegs);

        free_argvs(argvs, nsegs);
        free(segments);
    }

    free(line);
    return 0;
}