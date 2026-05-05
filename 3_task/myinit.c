
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <errno.h>
#include <time.h>

/* usleep is POSIX.1-2001 but not in strict C99 without _XOPEN_SOURCE */
extern int usleep(unsigned int useconds);

#define MAX_PROCS     64
#define MAX_LINE      1024
#define MAX_ARGS      32
#define LOG_FILE      "/tmp/myinit.log"
#define LOG_BUF       2200   /* достаточно для путей до MAX_LINE + метки */

typedef struct {
    char cmd[MAX_LINE];        /* вся строка целиком (для перезапуска) */
    char *argv[MAX_ARGS + 1];  /* argv для execv */
    char stdin_file[MAX_LINE];
    char stdout_file[MAX_LINE];
    pid_t pid;                 /* PID запущенного дочернего процесса, 0 если не запущен */
} ProcEntry;

static ProcEntry procs[MAX_PROCS];
static int       proc_count   = 0;
static char      config_path[MAX_LINE];
static int       log_fd       = -1;
static volatile sig_atomic_t need_reload = 0;


/* Запись строки в лог с временной меткой */
static void log_msg(const char *msg)
{
    if (log_fd < 0) return;

    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);

    char buf[LOG_BUF];
    int n = snprintf(buf, sizeof(buf), "[%s] %s\n", ts, msg);
    if (n > 0) write(log_fd, buf, (size_t)n);
}

/* Открыть / переоткрыть лог (append) */
static void open_log(void)
{
    if (log_fd >= 0) close(log_fd);
    log_fd = open(LOG_FILE, O_CREAT | O_WRONLY | O_APPEND, 0600);
    if (log_fd < 0) {
        /* Аварийный выход: некуда логировать */
        write(STDERR_FILENO, "myinit: cannot open log\n", 24);
        _exit(1);
    }
}

/* Разбить строку на токены (изменяет буфер) */
static int split_args(char *line, char **argv, int max_argv)
{
    int n = 0;
    char *p = line;
    while (*p && n < max_argv) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        argv[n++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    argv[n] = NULL;
    return n;
}

/* Проверить, что путь абсолютный */
static int is_absolute(const char *path)
{
    return path && path[0] == '/';
}


/* Освободить argv внутри записи */
static void free_entry(ProcEntry *e)
{
    memset(e, 0, sizeof(*e));
}

/* Прочитать конфигурационный файл, заполнить procs[] */
static int load_config(void)
{
    FILE *f = fopen(config_path, "r");
    if (!f) {
        char msg[LOG_BUF];
        snprintf(msg, sizeof(msg), "ERROR: cannot open config '%s': %s",
                 config_path, strerror(errno));
        log_msg(msg);
        return -1;
    }

    /* Сбросить старые записи */
    for (int i = 0; i < proc_count; i++)
        free_entry(&procs[i]);
    proc_count = 0;

    char line[MAX_LINE];
    int lineno = 0;
    while (fgets(line, sizeof(line), f) && proc_count < MAX_PROCS) {
        lineno++;
        /* Убрать \n */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Пропустить пустые строки и комментарии */
        if (len == 0 || line[0] == '#') continue;

        /* Сохранить исходную строку */
        ProcEntry *e = &procs[proc_count];
        strncpy(e->cmd, line, MAX_LINE - 1);
        e->cmd[MAX_LINE - 1] = '\0';

        /* Токенизировать: последние два токена — stdin и stdout файлы */
        char tmp[MAX_LINE];
        strncpy(tmp, line, MAX_LINE - 1);
        char *tokens[MAX_ARGS + 3];
        int n = split_args(tmp, tokens, MAX_ARGS + 2);

        if (n < 3) {
            char msg[128];
            snprintf(msg, sizeof(msg), "WARNING: line %d: need at least exe stdin stdout", lineno);
            log_msg(msg);
            continue;
        }

        /* Последние два — файлы перенаправления */
        strncpy(e->stdout_file, tokens[n-1], MAX_LINE - 1);
        strncpy(e->stdin_file,  tokens[n-2], MAX_LINE - 1);
        tokens[n-2] = NULL;   /* обрезаем до argv для exec */

        /* Проверяем абсолютность путей */
        if (!is_absolute(tokens[0])) {
            char msg[LOG_BUF];
            snprintf(msg, sizeof(msg), "WARNING: line %d: executable not absolute, skipping", lineno);
            log_msg(msg);
            continue;
        }
        if (!is_absolute(e->stdin_file)) {
            char msg[LOG_BUF];
            snprintf(msg, sizeof(msg), "WARNING: line %d: stdin path not absolute, skipping", lineno);
            log_msg(msg);
            continue;
        }
        if (!is_absolute(e->stdout_file)) {
            char msg[LOG_BUF];
            snprintf(msg, sizeof(msg), "WARNING: line %d: stdout path not absolute, skipping", lineno);
            log_msg(msg);
            continue;
        }

        /* Скопировать argv */
        for (int i = 0; tokens[i] != NULL && i < MAX_ARGS; i++) {
            /* argv хранятся указателями в e->cmd, нужно пересчитать */
            /* Используем cmd как буфер — перезаписываем его ещё раз */
        }

        /* Для корректных указателей argv разбираем cmd напрямую */
        /* Заново: cmd содержит "exe arg1 arg2 ... stdin stdout",
           нам нужно argv = {exe, arg1, ..., NULL} без двух последних */
        {
            /* сколько токенов cmd (без stdin/stdout) */
            int argc_exec = n - 2;
            /* разбить cmd в отдельный буфер внутри структуры,
               используем первые argc_exec токенов */
            /* Внимание: split_args модифицирует буфер,
               поэтому работаем с e->cmd */
            char *p = e->cmd;
            for (int i = 0; i < argc_exec && i < MAX_ARGS; i++) {
                while (*p == ' ' || *p == '\t') p++;
                e->argv[i] = p;
                while (*p && *p != ' ' && *p != '\t') p++;
                if (*p) *p++ = '\0';
            }
            e->argv[argc_exec > MAX_ARGS ? MAX_ARGS : argc_exec] = NULL;
        }

        e->pid = 0;
        proc_count++;
    }

    fclose(f);

    char msg[64];
    snprintf(msg, sizeof(msg), "Config loaded: %d entries", proc_count);
    log_msg(msg);
    return proc_count;
}


static void start_proc(int idx)
{
    ProcEntry *e = &procs[idx];
    pid_t pid = fork();

    if (pid < 0) {
        char msg[MAX_LINE];
        snprintf(msg, sizeof(msg), "ERROR: fork failed for entry %d: %s", idx, strerror(errno));
        log_msg(msg);
        return;
    }

    if (pid == 0) {
        /* --- дочерний процесс --- */

        /* Перенаправить stdin */
        int fd_in = open(e->stdin_file, O_RDONLY | O_CREAT, 0600);
        if (fd_in < 0) {
            /* Если не открылся — используем /dev/null */
            fd_in = open("/dev/null", O_RDONLY);
        }
        if (fd_in >= 0) {
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }

        /* Перенаправить stdout */
        int fd_out = open(e->stdout_file, O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (fd_out < 0) {
            fd_out = open("/dev/null", O_WRONLY);
        }
        if (fd_out >= 0) {
            dup2(fd_out, STDOUT_FILENO);
            dup2(fd_out, STDERR_FILENO);
            close(fd_out);
        }

        execv(e->argv[0], e->argv);
        /* Если execv вернулся — ошибка */
        _exit(127);
    }

    /* --- родительский процесс --- */
    e->pid = pid;
    char msg[MAX_LINE];
    snprintf(msg, sizeof(msg), "Started entry %d: pid=%d cmd='%s'", idx, pid, e->argv[0]);
    log_msg(msg);
}

/* Запустить все процессы */
static void start_all(void)
{
    for (int i = 0; i < proc_count; i++)
        start_proc(i);
}

/* Завершить все дочерние процессы */
static void kill_all(void)
{
    for (int i = 0; i < proc_count; i++) {
        if (procs[i].pid > 0) {
            kill(procs[i].pid, SIGTERM);
            char msg[128];
            snprintf(msg, sizeof(msg), "Sent SIGTERM to entry %d pid=%d", i, procs[i].pid);
            log_msg(msg);
        }
    }
    /* Дать процессам время завершиться */
    sleep(1);
    /* Принудительно убить оставшихся */
    for (int i = 0; i < proc_count; i++) {
        if (procs[i].pid > 0) {
            kill(procs[i].pid, SIGKILL);
        }
    }
    /* Собрать зомби */
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0) {}
    /* Сбросить pid */
    for (int i = 0; i < proc_count; i++)
        procs[i].pid = 0;
}


static void handler_hup(int sig)
{
    (void)sig;
    need_reload = 1;
}


static void daemonize(void)
{
    struct rlimit flim;
    int fd;

    /* Игнорировать терминальные сигналы */
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    /* fork — родитель уходит */
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid != 0)
        exit(0); /* родитель завершается */

    /* Новый сеанс — отвязываемся от терминала */
    setsid();

    /* Закрыть все файловые дескрипторы */
    getrlimit(RLIMIT_NOFILE, &flim);
    long max_fd = (flim.rlim_max == RLIM_INFINITY) ? 1024 : (long)flim.rlim_max;
    for (fd = 0; fd < max_fd; fd++)
        close(fd);

    /* Сменить каталог на корневой */
    if (chdir("/") != 0) {
        /* Некуда писать ошибку — уходим */
        _exit(1);
    }

    /* Открыть лог */
    open_log();
    log_msg("myinit daemon started");
}


static void main_loop(void)
{
    /* Установить обработчик SIGHUP */
    signal(SIGHUP, handler_hup);

    load_config();
    start_all();

    for (;;) {
        /* Если пришёл SIGHUP — перезагрузить конфиг */
        if (need_reload) {
            need_reload = 0;
            log_msg("SIGHUP received: reloading config");
            kill_all();
            load_config();
            start_all();
            continue;
        }

        /* Ждём любого завершившегося потомка (неблокирующе) */
        int status;
        pid_t dead = waitpid(-1, &status, WNOHANG);

        if (dead > 0) {
            /* Найти запись с этим pid */
            for (int i = 0; i < proc_count; i++) {
                if (procs[i].pid == dead) {
                    char msg[256];
                    if (WIFEXITED(status))
                        snprintf(msg, sizeof(msg),
                            "Child entry %d pid=%d exited with code %d, restarting",
                            i, dead, WEXITSTATUS(status));
                    else if (WIFSIGNALED(status))
                        snprintf(msg, sizeof(msg),
                            "Child entry %d pid=%d killed by signal %d, restarting",
                            i, dead, WTERMSIG(status));
                    else
                        snprintf(msg, sizeof(msg),
                            "Child entry %d pid=%d terminated (unknown), restarting",
                            i, dead);
                    log_msg(msg);
                    procs[i].pid = 0;
                    start_proc(i);
                    break;
                }
            }
        } else {
            /* Нет завершившихся — спим немного, чтобы не жечь CPU */
            usleep(100000); /* 100 мс */
        }
    }
}


int main(int argc, char *argv[])
{
    int opt;
    const char *cfg = NULL;

    while ((opt = getopt(argc, argv, "c:h")) != -1) {
        switch (opt) {
        case 'c':
            cfg = optarg;
            break;
        case 'h':
        default:
            fprintf(stderr, "Usage: %s -c <config_file>\n", argv[0]);
            return 1;
        }
    }

    /* Если -c не задан, берём первый позиционный аргумент (совместимость) */
    if (!cfg && optind < argc)
        cfg = argv[optind];

    if (!cfg) {
        fprintf(stderr, "Usage: %s -c <config_file>\n", argv[0]);
        return 1;
    }

    if (!is_absolute(cfg)) {
        fprintf(stderr, "myinit: config path must be absolute: %s\n", cfg);
        return 1;
    }

    strncpy(config_path, cfg, MAX_LINE - 1);
    config_path[MAX_LINE - 1] = '\0';

    daemonize();
    main_loop();

    return 0; /* never reached */
}
