/* myls.c — ls-минималка для Linux с -l -i -n -R -a -d, цветом,
 * выравниванием как у GNU ls и колонками в коротком режиме (как ls -C).
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>

struct opts { int l, i, n, R, a, d, color; };

struct item {
    char* name;
    struct stat st;
};

static int is_tty_stdout(void) { return isatty(STDOUT_FILENO); }

static void mode_to_str(mode_t m, char s[11])
{
    s[0] = S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' :
        S_ISCHR(m) ? 'c' : S_ISBLK(m) ? 'b' :
        S_ISFIFO(m) ? 'p' : S_ISSOCK(m) ? 's' : '-';
    s[1] = (m & S_IRUSR) ? 'r' : '-';
    s[2] = (m & S_IWUSR) ? 'w' : '-';
    s[3] = (m & S_IXUSR) ? 'x' : '-';
    s[4] = (m & S_IRGRP) ? 'r' : '-';
    s[5] = (m & S_IWGRP) ? 'w' : '-';
    s[6] = (m & S_IXGRP) ? 'x' : '-';
    s[7] = (m & S_IROTH) ? 'r' : '-';
    s[8] = (m & S_IWOTH) ? 'w' : '-';
    s[9] = (m & S_IXOTH) ? 'x' : '-';
    if (m & S_ISUID) s[3] = (s[3] == 'x') ? 's' : 'S';
    if (m & S_ISGID) s[6] = (s[6] == 'x') ? 's' : 'S';
    if (m & S_ISVTX) s[9] = (s[9] == 'x') ? 't' : 'T';
    s[10] = '\0';
}

static const char* color_code(mode_t m)
{
    if (S_ISDIR(m))  return "\033[01;34m";
    if (S_ISLNK(m))  return "\033[01;36m";
    if (S_ISSOCK(m)) return "\033[01;35m";
    if (S_ISFIFO(m)) return "\033[33m";
    if (S_ISBLK(m))  return "\033[01;33m";
    if (S_ISCHR(m))  return "\033[01;33m";
    if (S_ISREG(m) && (m & (S_IXUSR | S_IXGRP | S_IXOTH)))
        return "\033[01;32m";
    return NULL;
}

static void print_name_colored(const char* name, mode_t m, const struct opts* o)
{
    const char* c = NULL;
    if ((o->color == 2) || (o->color == 1 && is_tty_stdout()))
        c = color_code(m);
    if (c) {
        fputs(c, stdout);
        fputs(name, stdout);
        fputs("\033[0m", stdout);
    }
    else {
        fputs(name, stdout);
    }
}

static void format_time(time_t t, char* buf, size_t sz)
{
    time_t now = time(NULL);
    struct tm tm;
    long six_months = (long)(365.2425 / 2 * 24 * 3600);
    localtime_r(&t, &tm);
    if (labs((long)(now - t)) > six_months)
        strftime(buf, sz, "%b %e  %Y", &tm);
    else
        strftime(buf, sz, "%b %e %H:%M", &tm);
}

static int sel_all(const struct dirent* e) { (void)e; return 1; }
static int sel_visible(const struct dirent* e) { return e->d_name[0] != '.'; }

static int digits_uintmax(uintmax_t x)
{
    int d = 1;
    while (x >= 10) { x /= 10; d++; }
    return d;
}

static char* owner_str(uid_t uid, int numeric, char* buf, size_t sz)
{
    if (numeric) {
        snprintf(buf, sz, "%u", (unsigned)uid);
        return buf;
    }
    else {
        struct passwd* pw = getpwuid(uid);
        if (pw) return pw->pw_name;
        snprintf(buf, sz, "%u", (unsigned)uid);
        return buf;
    }
}

static char* group_str(gid_t gid, int numeric, char* buf, size_t sz)
{
    if (numeric) {
        snprintf(buf, sz, "%u", (unsigned)gid);
        return buf;
    }
    else {
        struct group* gr = getgrgid(gid);
        if (gr) return gr->gr_name;
        snprintf(buf, sz, "%u", (unsigned)gid);
        return buf;
    }
}

static int by_name(const struct dirent** a, const struct dirent** b)
{
    return alphasort(a, b);
}

/* собираем список записей каталога (имя + stat) и вычисляем ширины колонок */
static int scandir_items(const char* path, const struct opts* o,
    struct item** out, int* inode_w,
    int* links_w, int* owner_w, int* group_w, int* size_w,
    blkcnt_t* total_blocks, size_t* max_name_len)
{
    struct dirent** nl = NULL;
    int n, i;
    struct item* items = NULL;
    blkcnt_t blocks = 0;
    int iw = 0, lw = 0, ow = 0, gw = 0, sw = 0;
    size_t mnl = 0;

    n = scandir(path, &nl, o->a ? sel_all : sel_visible, by_name);
    if (n < 0) return -1;

    items = (struct item*)calloc((size_t)n, sizeof(*items));
    if (!items) {
        for (i = 0; i < n; i++) free(nl[i]);
        free(nl);
        errno = ENOMEM;
        return -1;
    }

    for (i = 0; i < n; i++) {
        char p[PATH_MAX];
        struct stat st;

        snprintf(p, sizeof p, "%s/%s", path, nl[i]->d_name);
        if (lstat(p, &st) != 0) {
            fprintf(stderr, "myls: %s: %s\n", nl[i]->d_name, strerror(errno));
            free(nl[i]);
            items[i].name = NULL;
            continue;
        }

        items[i].name = strdup(nl[i]->d_name);   /* копируем имя */
        free(nl[i]);
        if (!items[i].name) {
            int k;
            for (k = 0; k < i; k++) free(items[k].name);
            free(items);
            free(nl);
            errno = ENOMEM;
            return -1;
        }
        items[i].st = st;

        if (strlen(items[i].name) > mnl) mnl = strlen(items[i].name);

        if (o->i) {
            int d = digits_uintmax((uintmax_t)st.st_ino);
            if (d > iw) iw = d;
        }
        if (o->l) {
            char buf[32];
            int d;

            d = digits_uintmax((uintmax_t)st.st_nlink);
            if (d > lw) lw = d;

            d = (int)strlen(owner_str(st.st_uid, o->n, buf, sizeof buf));
            if (d > ow) ow = d;

            d = (int)strlen(group_str(st.st_gid, o->n, buf, sizeof buf));
            if (d > gw) gw = d;

            d = digits_uintmax((uintmax_t)st.st_size);
            if (d > sw) sw = d;

            blocks += st.st_blocks; /* 512B blocks */
        }
    }
    free(nl);

    /* компактно выкидываем элементы с name == NULL */
    {
        int w = 0;
        for (i = 0; i < n; i++) {
            if (items[i].name) items[w++] = items[i];
        }
        n = w;
    }

    *out = items;
    if (inode_w) *inode_w = iw;
    if (links_w) *links_w = lw;
    if (owner_w) *owner_w = ow;
    if (group_w) *group_w = gw;
    if (size_w)  *size_w = sw;
    if (total_blocks) *total_blocks = blocks;
    if (max_name_len) *max_name_len = mnl;
    return n;
}

static void print_long_line(const struct item* it, const struct opts* o,
    int inode_w, int links_w, int owner_w, int group_w, int size_w)
{
    char m[11], tbuf[32], obuf[32], gbuf[32];

    if (o->i) printf("%*ju ", inode_w, (uintmax_t)it->st.st_ino);

    mode_to_str(it->st.st_mode, m);
    format_time(it->st.st_mtime, tbuf, sizeof tbuf);

    printf("%s %*ju %-*s %-*s %*ju %s ",
        m,
        links_w, (uintmax_t)it->st.st_nlink,
        owner_w, owner_str(it->st.st_uid, o->n, obuf, sizeof obuf),
        group_w, group_str(it->st.st_gid, o->n, gbuf, sizeof gbuf),
        size_w, (uintmax_t)it->st.st_size,
        tbuf);
    print_name_colored(it->name, it->st.st_mode, o);
    putchar('\n');
}

/* печать короткого режима в несколько колонок (как ls -C) при выводе в TTY */
static void print_columns(const struct item* items, int n,
    const struct opts* o, int inode_w, size_t max_name_len)
{
    struct winsize ws;
    int is_tty = is_tty_stdout();
    int termw = 80;
    size_t gap = 2;
    size_t cellw;
    int cols, rows;
    int r, c;

    if (!is_tty) {
        /* не TTY: по одному на строку */
        for (int i = 0; i < n; i++) {
            if (o->i) printf("%*ju ", inode_w, (uintmax_t)items[i].st.st_ino);
            print_name_colored(items[i].name, items[i].st.st_mode, o);
            putchar('\n');
        }
        return;
    }

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        termw = ws.ws_col;

    /* ширина ячейки: [inode + пробел] + имя + 2 пробела */
    cellw = (o->i ? (size_t)inode_w + 1 : 0) + max_name_len + gap;
    if (cellw < 1) cellw = 1;

    cols = termw / (int)cellw;
    if (cols < 1) cols = 1;

    rows = (n + cols - 1) / cols;

    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            int idx = r + c * rows;
            size_t printed = 0;
            if (idx >= n) break;

            if (o->i) {
                printf("%*ju ", inode_w, (uintmax_t)items[idx].st.st_ino);
                printed += (size_t)inode_w + 1;
            }

            print_name_colored(items[idx].name, items[idx].st.st_mode, o);
            printed += strlen(items[idx].name);

            /* паддинг между колонками, кроме последней занятой */
            if (c < cols - 1) {
                /* если следующего элемента в следующей колонке нет — не паддим */
                int next_idx = r + (c + 1) * rows;
                if (next_idx < n) {
                    size_t pad = cellw > printed ? cellw - printed : gap;
                    for (size_t k = 0; k < pad; k++) putchar(' ');
                }
            }
        }
        putchar('\n');
    }
}

static void list_dir(const char* path, const struct opts* o, int header)
{
    struct item* items = NULL;
    int n, i, inode_w = 0, links_w = 0, owner_w = 0, group_w = 0, size_w = 0;
    blkcnt_t blocks = 0;
    size_t max_name_len = 0;

    n = scandir_items(path, o, &items, &inode_w, &links_w, &owner_w, &group_w, &size_w, &blocks, &max_name_len);
    if (n < 0) {
        fprintf(stderr, "myls: cannot open '%s': %s\n", path, strerror(errno));
        return;
    }

    if (header) printf("%s:\n", path);

    if (o->l) {
        printf("total %ju\n", (uintmax_t)(blocks / 2));
        for (i = 0; i < n; i++)
            print_long_line(&items[i], o, inode_w, links_w, owner_w, group_w, size_w);
    }
    else {
        print_columns(items, n, o, inode_w, max_name_len);
    }

    if (o->R) {
        for (i = 0; i < n; i++) {
            const struct item* it = &items[i];
            if (!S_ISDIR(it->st.st_mode) || S_ISLNK(it->st.st_mode)) continue;
            if (strcmp(it->name, ".") == 0 || strcmp(it->name, "..") == 0) continue;

            char p[PATH_MAX];
            snprintf(p, sizeof p, "%s/%s", path, it->name);
            putchar('\n');
            list_dir(p, o, 1);
        }
    }

    for (i = 0; i < n; i++) free(items[i].name);
    free(items);
}

static int parse_color(const char* s)
{
    if (!s || !*s) return 1;        /* auto */
    if (!strcmp(s, "always")) return 2;
    if (!strcmp(s, "auto"))   return 1;
    if (!strcmp(s, "never"))  return 0;
    return 1;
}

int main(int argc, char** argv)
{
    struct opts o = { 0 };
    int i, j;

    o.color = parse_color(getenv("MYLS_COLOR"));

    for (i = 1; i < argc; i++) {
        const char* s = argv[i];
        if (s[0] != '-') { fprintf(stderr, "myls: operands not supported\n"); return 2; }
        if (!strcmp(s, "--")) break;
        for (j = 1; s[j]; j++) {
            switch (s[j]) {
            case 'l': o.l = 1; break;
            case 'i': o.i = 1; break;
            case 'n': o.n = 1; o.l = 1; break; /* -n включает long */
            case 'R': o.R = 1; break;
            case 'a': o.a = 1; break;
            case 'd': o.d = 1; break;
            default:
                fprintf(stderr, "myls: unknown option -- %c\n", s[j]);
                return 2;
            }
        }
    }

    if (o.d) {
        struct item it;
        if (lstat(".", &it.st) != 0) { perror("myls"); return 1; }
        it.name = strdup(".");
        if (!it.name) { perror("myls"); return 1; }

        if (o.l) {
            int inode_w = o.i ? digits_uintmax((uintmax_t)it.st.st_ino) : 0;
            int links_w = digits_uintmax((uintmax_t)it.st.st_nlink);
            char b[32];
            int owner_w = (int)strlen(owner_str(it.st.st_uid, o.n, b, sizeof b));
            int group_w = (int)strlen(group_str(it.st.st_gid, o.n, b, sizeof b));
            int size_w = digits_uintmax((uintmax_t)it.st.st_size);
            print_long_line(&it, &o, inode_w, links_w, owner_w, group_w, size_w);
        }
        else {
            /* короткий режим: колонками/или построчно в зависимости от TTY */
            struct item one[1] = { it };
            size_t max_name_len = strlen(it.name);
            int inode_w = o.i ? digits_uintmax((uintmax_t)it.st.st_ino) : 0;
            print_columns(one, 1, &o, inode_w, max_name_len);
        }
        free(it.name);
    }
    else {
        if (o.R) list_dir(".", &o, 1); else list_dir(".", &o, 0);
    }
    return 0;
}