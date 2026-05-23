/*
 * client.c — клиент "броуновского бота".
 *
 * Поведение по умолчанию (как требует условие задачи):
 *   - читает из stdin строки с числами (длиной не более 10 символов)
 *   - отправляет их серверу по UNIX-сокету, имя которого взято из config
 *   - печатает ответы сервера в stdout
 *
 * Тестовый режим (-f FILE -d MAX_DELAY -s STATS):
 *   - читает FILE побайтово
 *   - после случайного числа байт (1..255) делает sleep на случайное
 *     время в [0..MAX_DELAY) секунд
 *   - встретив '\n' — отправляет накопленную строку как число серверу
 *   - сохраняет суммарную задержку и временные метки в STATS-файл
 *
 * Параметры:
 *   -c PATH      файл config (по умолчанию "config")
 *   -f FILE      входной файл с числами (вместо stdin)
 *   -d SECONDS   максимальное значение случайной задержки (по умолчанию 0)
 *   -s FILE      файл со статистикой клиента
 *   -q           тихий режим (не печатать ответы)
 *   -i ID        строковый идентификатор клиента для логов/статистики
 *   -h           справка
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

/* ---------- маленькие утилиты ---------- */

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void die(const char *what)
{
    fprintf(stderr, "client: %s: %s\n", what, strerror(errno));
    exit(EXIT_FAILURE);
}

/* Считать имя сокета из config-файла. */
static void load_socket_path(const char *cfg, char *out, size_t outsz)
{
    FILE *f = fopen(cfg, "r");
    if (!f) {
        fprintf(stderr, "клиент: не удалось открыть конфиг '%s': %s\n",
                cfg, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (!fgets(out, (int)outsz, f)) {
        fprintf(stderr, "клиент: конфиг '%s' пуст\n", cfg);
        fclose(f);
        exit(EXIT_FAILURE);
    }
    fclose(f);
    size_t l = strlen(out);
    while (l > 0 && (out[l-1] == '\n' || out[l-1] == '\r')) out[--l] = '\0';
    if (l == 0) {
        fprintf(stderr, "клиент: пустой путь к сокету в конфиге\n");
        exit(EXIT_FAILURE);
    }
}

/* Подключиться к серверу. Возвращает fd. */
static int connect_server(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) die("socket");

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "клиент: слишком длинный путь к сокету: %s\n", path);
        exit(EXIT_FAILURE);
    }
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    /* Сервер может ещё не успеть слушать — сделаем несколько попыток. */
    int attempts = 50;
    while (attempts-- > 0) {
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) return fd;
        if (errno != ENOENT && errno != ECONNREFUSED) die("connect");
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 }; // 100 мс
        nanosleep(&ts, NULL);
    }
    fprintf(stderr, "клиент: не удалось подключиться к %s после всех попыток\n", path);
    exit(EXIT_FAILURE);
}

/* Отправляет ровно len байт, переотправляя при коротких write. */
static int write_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

/* Читает из fd до символа '\n' (включительно), кладёт в buf.
 * Возвращает число прочитанных байт без '\n', либо -1. */
static ssize_t read_line(int fd, char *buf, size_t bufsz)
{
    size_t pos = 0;
    while (pos + 1 < bufsz) {
        char ch;
        ssize_t n = read(fd, &ch, 1);
        if (n == 0) return -1;
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (ch == '\n') {
            buf[pos] = '\0';
            return (ssize_t)pos;
        }
        buf[pos++] = ch;
    }
    buf[pos] = '\0';
    return (ssize_t)pos;
}

/* ---------- режимы работы ---------- */

/* обычный режим: stdin -> сервер -> stdout. */
static int run_stdin(int sfd, int quiet)
{
    char line[MAX_LINE_LEN + 1];
    while (fgets(line, sizeof(line), stdin)) {
        size_t l = strlen(line);
        if (l == 0) continue;
        /* Гарантируем перевод строки на конце. */
        if (line[l-1] != '\n') {
            if (l + 1 >= sizeof(line)) {
                fprintf(stderr, "клиент: строка ввода слишком длинная\n");
                return EXIT_FAILURE;
            }
            line[l++] = '\n';
            line[l]   = '\0';
        }
        if (write_all(sfd, line, l) < 0) {
            perror("клиент: запись");
            return EXIT_FAILURE;
        }
        char reply[MAX_LINE_LEN];
        ssize_t rn = read_line(sfd, reply, sizeof(reply));
        if (rn < 0) {
            fprintf(stderr, "клиент: сервер закрыл соединение\n");
            return EXIT_FAILURE;
        }
        if (!quiet) printf("%s\n", reply);
    }
    return 0;
}

/* тестовый режим: побайтовое чтение файла + случайные задержки. */
static int run_file(int sfd, const char *path, double max_delay,
                    const char *stats_path, const char *id, int quiet)
{
    FILE *fin = fopen(path, "r");
    if (!fin) {
        fprintf(stderr, "клиент: не удалось открыть '%s': %s\n", path, strerror(errno));
        return EXIT_FAILURE;
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    srand((unsigned)(ts.tv_nsec ^ (getpid() * 2654435761u)));

    char    linebuf[MAX_LINE_LEN + 1];
    size_t  lpos = 0;
    int     bytes_until_pause = 1 + (rand() % 255);
    double  total_delay = 0.0;
    long    pause_count = 0;
    long    numbers_sent = 0;

    double t_first = -1.0, t_last = -1.0;

    int byte;
    while ((byte = fgetc(fin)) != EOF) {
        /* Вставляем случайную паузу. */
        if (--bytes_until_pause <= 0) {
            bytes_until_pause = 1 + (rand() % 255);
            if (max_delay > 0.0) {
                double d = ((double)rand() / (double)RAND_MAX) * max_delay;
                struct timespec slp;
                slp.tv_sec  = (time_t)d;
                slp.tv_nsec = (long)((d - (double)slp.tv_sec) * 1e9);
                nanosleep(&slp, NULL);
                total_delay += d;
                pause_count++;
            }
        }

        if (byte == '\n') {
            if (lpos == 0) continue;       /* пустая строка */
            linebuf[lpos++] = '\n';
            linebuf[lpos]   = '\0';

            double t = now_sec();
            if (t_first < 0) t_first = t;
            t_last = t;

            if (write_all(sfd, linebuf, lpos) < 0) {
                fprintf(stderr, "клиент[%s]: ошибка записи: %s\n", id, strerror(errno));
                fclose(fin);
                return EXIT_FAILURE;
            }
            char reply[MAX_LINE_LEN];
            ssize_t rn = read_line(sfd, reply, sizeof(reply));
            if (rn < 0) {
                fprintf(stderr, "клиент[%s]: сервер закрыл соединение\n", id);
                fclose(fin);
                return EXIT_FAILURE;
            }
            if (!quiet) printf("[%s] -> %s\n", id, reply);
            numbers_sent++;
            lpos = 0;
        } else {
            if (lpos < MAX_LINE_LEN) linebuf[lpos++] = (char)byte;
            /* Длинная строка обрежется до MAX_LINE_LEN — для
             * нашего теста это не страшно, входные числа короче. */
        }
    }
    /* Если в файле последнее число без \n — отправим. */
    if (lpos > 0) {
        linebuf[lpos++] = '\n';
        linebuf[lpos]   = '\0';
        double t = now_sec();
        if (t_first < 0) t_first = t;
        t_last = t;
        if (write_all(sfd, linebuf, lpos) >= 0) {
            char reply[MAX_LINE_LEN];
            (void)read_line(sfd, reply, sizeof(reply));
            numbers_sent++;
        }
    }

    fclose(fin);

    /* Сохраним статистику */
    if (stats_path && *stats_path) {
        FILE *fs = fopen(stats_path, "w");
        if (!fs) {
            fprintf(stderr, "клиент[%s]: не удалось открыть файл статистики '%s': %s\n",
                    id, stats_path, strerror(errno));
        } else {
            fprintf(fs, "id=%s\n",            id ? id : "");
            fprintf(fs, "pid=%d\n",           (int)getpid());
            fprintf(fs, "numbers_sent=%ld\n", numbers_sent);
            fprintf(fs, "pauses=%ld\n",       pause_count);
            fprintf(fs, "total_delay=%.9f\n", total_delay);
            fprintf(fs, "t_first=%.9f\n",     t_first);
            fprintf(fs, "t_last=%.9f\n",      t_last);
            fclose(fs);
        }
    }

    return 0;
}

static void usage(const char *p)
{
    fprintf(stderr,
        "Использование: %s [-c конфиг] [-f файл -d задержка -s статистика -i id] [-q]\n"
        "  -c ПУТЬ     файл конфигурации (по умолчанию: %s)\n"
        "  -f ФАЙЛ     читать числа из ФАЙЛ побайтово (тестовый режим)\n"
        "  -d СЕКУНДЫ  максимальная случайная задержка в секундах (по умолчанию: 0)\n"
        "  -s ФАЙЛ     записать статистику клиента сюда\n"
        "  -i ID       строковый идентификатор клиента\n"
        "  -q          тихий режим — не выводить ответы сервера\n"
        "  -h          показать эту справку\n",
        p, DEFAULT_CONFIG);
}

int main(int argc, char **argv)
{
    const char *cfg     = DEFAULT_CONFIG;
    const char *file    = NULL;
    const char *stats   = NULL;
    const char *id      = "client";
    double      delay   = 0.0;
    int         quiet   = 0;

    int opt;
    while ((opt = getopt(argc, argv, "c:f:d:s:i:qh")) != -1) {
        switch (opt) {
            case 'c': cfg   = optarg; break;
            case 'f': file  = optarg; break;
            case 'd': delay = atof(optarg); break;
            case 's': stats = optarg; break;
            case 'i': id    = optarg; break;
            case 'q': quiet = 1;      break;
            case 'h': usage(argv[0]); return 0;
            default:  usage(argv[0]); return EXIT_FAILURE;
        }
    }

    char sock_path[256];
    load_socket_path(cfg, sock_path, sizeof(sock_path));

    int sfd = connect_server(sock_path);

    int rc = file ? run_file(sfd, file, delay, stats, id, quiet)
                  : run_stdin(sfd, quiet);

    close(sfd);
    return rc;
}
