/*
 * server.c — сервер "броуновский бот".
 *
 * Однопоточный сервер на UNIX stream сокете с асинхронным
 * мультиплексированием через poll(2). Поддерживает много
 * одновременных клиентов; на каждого клиента ведёт буфер
 * приёма (для незавершённых строк) и буфер отправки
 * (если клиент медленно читает).
 *
 * Параметры командной строки (через getopt):
 *   -c <path>   путь к файлу конфигурации (по умолчанию "config")
 *   -l <path>   путь к лог-файлу (по умолчанию "logs/server.log")
 *   -h          справка
 *
 * Файл config содержит одну строку — имя UNIX-сокета в /tmp.
 *
 * Логирование:
 *   - При подключении клиента — fd и значение sbrk(0) (для контроля
 *     утечек дескрипторов / памяти).
 *   - Каждый запрос/ответ — для отладки.
 *   - Отключения клиентов.
 */

#define _GNU_SOURCE          /* для accept4 на Linux; на macOS не мешает */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

/* На macOS sbrk убран из публичных заголовков в новых SDK.
 * Объявляем вручную — символ в libc ещё присутствует. */
#if defined(__APPLE__)
extern void *sbrk(int);
#endif

/* ------------------------------------------------------------------ */
/*  Глобальное состояние                                              */
/* ------------------------------------------------------------------ */

static long long g_state = 0;          /* внутреннее состояние сервера */
static FILE     *g_log    = NULL;      /* лог-файл                    */
static volatile sig_atomic_t g_stop = 0; /* флаг остановки           */
static char      g_sock_path[256] = {0}; /* путь к сокету (для очистки) */

/* ------------------------------------------------------------------ */
/*  Логирование                                                       */
/* ------------------------------------------------------------------ */

static void log_msg(const char *fmt, ...)
{
    if (!g_log) return;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    fprintf(g_log, "%lld.%09ld ", (long long)ts.tv_sec, ts.tv_nsec);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);

    fputc('\n', g_log);
    fflush(g_log);
}

static void die(const char *what)
{
    fprintf(stderr, "сервер: %s: %s\n", what, strerror(errno));
    if (g_log) log_msg("FATAL %s: %s", what, strerror(errno));
    exit(EXIT_FAILURE);
}

/* ------------------------------------------------------------------ */
/*  Структура клиента                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    int   fd;                          /* дескриптор сокета */
    char  rbuf[CLIENT_RECV_BUF];       /* буфер приёма       */
    size_t rlen;                       /* сколько в нём данных */
    char  wbuf[CLIENT_SEND_BUF];       /* буфер отправки     */
    size_t wlen;                       /* сколько ждёт отправки */
} client_t;

/* Динамический массив клиентов, параллельный pollfd-массиву.
 * Индексация: [0] — слушающий сокет; далее — клиенты. */
static struct pollfd *g_pfds    = NULL;
static client_t     **g_clients = NULL;   /* g_clients[0] не используется */
static size_t         g_nfds    = 0;
static size_t         g_cap     = 0;

/* ------------------------------------------------------------------ */
/*  Управление массивом клиентов                                      */
/* ------------------------------------------------------------------ */

static void ensure_capacity(size_t need)
{
    if (need <= g_cap) return;
    size_t ncap = g_cap ? g_cap * 2 : 16;
    while (ncap < need) ncap *= 2;

    struct pollfd *np = realloc(g_pfds, ncap * sizeof(*np));
    client_t      **nc = realloc(g_clients, ncap * sizeof(*nc));
    if (!np || !nc) die("realloc");
    g_pfds    = np;
    g_clients = nc;
    g_cap     = ncap;
}

static void client_add(int fd)
{
    ensure_capacity(g_nfds + 1);

    client_t *c = calloc(1, sizeof(*c));
    if (!c) die("calloc");
    c->fd = fd;

    g_pfds[g_nfds].fd      = fd;
    g_pfds[g_nfds].events  = POLLIN;     /* пока только читаем */
    g_pfds[g_nfds].revents = 0;
    g_clients[g_nfds]      = c;
    g_nfds++;

    /* Лог подключения: fd и граница кучи — для контроля утечек.
     * Запрошено явно в условии задачи. */
    void *brk = sbrk(0);
    log_msg("CONNECT fd=%d sbrk=%p clients=%zu", fd, brk, g_nfds - 1);
}

static void client_remove(size_t idx)
{
    if (idx == 0 || idx >= g_nfds) return;
    client_t *c = g_clients[idx];
    log_msg("DISCONNECT fd=%d clients=%zu", c->fd, g_nfds - 2);
    close(c->fd);
    free(c);

    /* Перемещаем последний на место удалённого, чтобы не двигать всё. */
    size_t last = g_nfds - 1;
    if (idx != last) {
        g_pfds[idx]    = g_pfds[last];
        g_clients[idx] = g_clients[last];
    }
    g_nfds--;
}

/* ------------------------------------------------------------------ */
/*  Обработка одной завершённой строки от клиента                     */
/* ------------------------------------------------------------------ */

/* Добавляет данные в выходной буфер клиента; если не помещаются —
 * это сигнал, что клиент совсем не читает; такое подключение закроем. */
static int client_enqueue(client_t *c, const char *data, size_t len)
{
    if (c->wlen + len > sizeof(c->wbuf)) {
        log_msg("ERROR fd=%d send buffer overflow, drop", c->fd);
        return -1;
    }
    memcpy(c->wbuf + c->wlen, data, len);
    c->wlen += len;
    return 0;
}

/* Разбирает строку line (без '\n'), применяет к состоянию,
 * формирует ответ и кладёт в исходящий буфер.
 * Возвращает 0 на успехе, -1 если клиента надо отключить. */
static int handle_line(client_t *c, const char *line, size_t llen)
{
    /* Пустые строки игнорируем (но они валидны — например, лишний \n). */
    if (llen == 0) return 0;

    /* Скопируем во временный буфер, чтобы можно было поставить '\0'
     * для strtoll. CLIENT_RECV_BUF — потолок длины. */
    char tmp[MAX_LINE_LEN + 1];
    if (llen > MAX_LINE_LEN) {
        log_msg("ERROR fd=%d line too long (%zu), drop", c->fd, llen);
        return -1;
    }
    memcpy(tmp, line, llen);
    tmp[llen] = '\0';

    /* Уберём пробелы по краям (на случай \r и пр.). */
    char *p = tmp;
    while (*p && isspace((unsigned char)*p)) p++;
    char *end = tmp + strlen(tmp);
    while (end > p && isspace((unsigned char)end[-1])) *--end = '\0';

    if (*p == '\0') return 0;   /* пустая строка после очистки */

    errno = 0;
    char *parse_end = NULL;
    long long v = strtoll(p, &parse_end, 10);
    if (errno != 0 || parse_end == p || *parse_end != '\0') {
        log_msg("ERROR fd=%d bad number '%s'", c->fd, p);
        /* Не отключаем — просто игнорируем мусор. */
        return 0;
    }

    g_state += v;

    char reply[MAX_LINE_LEN];
    int  rn = snprintf(reply, sizeof(reply), "%lld\n", g_state);
    if (rn <= 0 || (size_t)rn >= sizeof(reply)) {
        log_msg("ERROR fd=%d reply formatting failed", c->fd);
        return -1;
    }

    log_msg("REQ fd=%d in=%lld state=%lld", c->fd, v, g_state);

    if (client_enqueue(c, reply, (size_t)rn) < 0) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Обработка событий чтения/записи                                    */
/* ------------------------------------------------------------------ */

/* Возвращает 1 если клиента нужно удалить. */
static int client_on_read(client_t *c)
{
    /* Читаем столько, сколько влезает в свободное место rbuf. */
    size_t free_space = sizeof(c->rbuf) - c->rlen;
    if (free_space == 0) {
        /* Слишком длинная строка без '\n'. Превышен лимит — отключим. */
        log_msg("ERROR fd=%d recv buffer overflow", c->fd);
        return 1;
    }

    ssize_t n = recv(c->fd, c->rbuf + c->rlen, free_space, 0);
    if (n == 0) {
        /* Клиент аккуратно закрыл соединение. */
        return 1;
    }
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
        log_msg("ERROR fd=%d recv: %s", c->fd, strerror(errno));
        return 1;
    }
    c->rlen += (size_t)n;

    /* Извлекаем все полные строки. Незавершённый хвост остаётся в буфере —
     * это и есть требуемая буферизация ввода. */
    size_t start = 0;
    for (size_t i = 0; i < c->rlen; ++i) {
        if (c->rbuf[i] == '\n') {
            if (handle_line(c, c->rbuf + start, i - start) < 0) return 1;
            start = i + 1;
        }
    }
    /* Сдвигаем остаток в начало буфера. */
    if (start > 0) {
        memmove(c->rbuf, c->rbuf + start, c->rlen - start);
        c->rlen -= start;
    }
    return 0;
}

/* Возвращает 1 если клиента нужно удалить. */
static int client_on_write(client_t *c)
{
    if (c->wlen == 0) return 0;
    ssize_t n = send(c->fd, c->wbuf, c->wlen, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
        log_msg("ERROR fd=%d send: %s", c->fd, strerror(errno));
        return 1;
    }
    if ((size_t)n < c->wlen) {
        memmove(c->wbuf, c->wbuf + n, c->wlen - n);
    }
    c->wlen -= (size_t)n;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Конфигурация и сетевая инициализация                              */
/* ------------------------------------------------------------------ */

/* Читает имя сокета из файла config. Если файла нет — пишет туда дефолт.
 * Возвращает путь в out (размер outsz). */
static void read_or_create_config(const char *cfg_path, char *out, size_t outsz)
{
    FILE *f = fopen(cfg_path, "r");
    if (f) {
        if (!fgets(out, (int)outsz, f)) {
            fclose(f);
            fprintf(stderr, "сервер: файл конфигурации '%s' пуст\n", cfg_path);
            exit(EXIT_FAILURE);
        }
        fclose(f);
        /* Убираем перевод строки. */
        size_t l = strlen(out);
        while (l > 0 && (out[l-1] == '\n' || out[l-1] == '\r')) out[--l] = '\0';
        if (l == 0) {
            fprintf(stderr, "сервер: файл конфигурации '%s' содержит пустой путь\n", cfg_path);
            exit(EXIT_FAILURE);
        }
        return;
    }
    /* Нет файла — создадим с дефолтным путём. */
    snprintf(out, outsz, "%s", DEFAULT_SOCKET_PATH);
    f = fopen(cfg_path, "w");
    if (!f) die("fopen config for writing");
    fprintf(f, "%s\n", out);
    fclose(f);
}

static int make_listening_socket(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) die("socket");

    /* Если сокет с таким именем уже существует, удалим — иначе bind упадёт. */
    (void)unlink(path);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "сервер: слишком длинный путь к сокету: %s\n", path);
        exit(EXIT_FAILURE);
    }
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) die("bind");
    if (listen(fd, 128) < 0) die("listen");

    /* Слушающий сокет тоже сделаем неблокирующим. */
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) die("fcntl listen O_NONBLOCK");

    return fd;
}

/* Принять как можно больше входящих подключений (на случай, если их
 * прилетело несколько за один вызов poll). */
static void accept_loop(int lfd)
{
    for (;;) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            if (errno == EINTR) continue;
            log_msg("ERROR accept: %s", strerror(errno));
            return;
        }
        int fl = fcntl(cfd, F_GETFL, 0);
        if (fl < 0 || fcntl(cfd, F_SETFL, fl | O_NONBLOCK) < 0) {
            log_msg("ERROR fcntl client O_NONBLOCK: %s", strerror(errno));
            close(cfd);
            continue;
        }
        client_add(cfd);
    }
}

/* ------------------------------------------------------------------ */
/*  Завершение                                                         */
/* ------------------------------------------------------------------ */

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void cleanup(void)
{
    /* Записать финальную метрику для проверки утечек. */
    if (g_log) {
        void *brk = sbrk(0);
        log_msg("SHUTDOWN sbrk=%p state=%lld", brk, g_state);
    }
    /* Закрыть все клиентские fd. */
    for (size_t i = 1; i < g_nfds; ++i) {
        if (g_clients[i]) {
            close(g_clients[i]->fd);
            free(g_clients[i]);
        }
    }
    if (g_nfds > 0) close(g_pfds[0].fd);
    free(g_pfds);
    free(g_clients);
    if (g_sock_path[0]) unlink(g_sock_path);
    if (g_log) fclose(g_log);
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

static void usage(const char *p)
{
    fprintf(stderr,
        "Использование: %s [-c конфиг] [-l лог]\n"
        "  -c <путь>  файл конфигурации с путём к сокету (по умолчанию: %s)\n"
        "  -l <путь>  файл лога (по умолчанию: logs/server.log)\n"
        "  -h         показать эту справку\n",
        p, DEFAULT_CONFIG);
}

int main(int argc, char **argv)
{
    const char *cfg_path = DEFAULT_CONFIG;
    const char *log_path = "logs/server.log";

    int opt;
    while ((opt = getopt(argc, argv, "c:l:h")) != -1) {
        switch (opt) {
            case 'c': cfg_path = optarg; break;
            case 'l': log_path = optarg; break;
            case 'h': usage(argv[0]); return 0;
            default:  usage(argv[0]); return EXIT_FAILURE;
        }
    }

    /* Логи открываем в append, чтобы перезапуск не стирал историю. */
    g_log = fopen(log_path, "a");
    if (!g_log) {
        fprintf(stderr, "сервер: не удалось открыть лог '%s': %s\n",
                log_path, strerror(errno));
        return EXIT_FAILURE;
    }
    setvbuf(g_log, NULL, _IOLBF, 0);

    read_or_create_config(cfg_path, g_sock_path, sizeof(g_sock_path));
    log_msg("START socket=%s pid=%d", g_sock_path, (int)getpid());

    /* SIGPIPE — иначе при write на закрытый сокет процесс упадёт. */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    atexit(cleanup);

    int lfd = make_listening_socket(g_sock_path);

    /* Слушающий сокет — первая запись в pollfd-массиве. */
    ensure_capacity(1);
    g_pfds[0].fd      = lfd;
    g_pfds[0].events  = POLLIN;
    g_pfds[0].revents = 0;
    g_clients[0]      = NULL;
    g_nfds = 1;

    void *brk_start = sbrk(0);
    log_msg("INIT sbrk=%p", brk_start);

    /* ------------------------------ главный цикл ------------------------------ */
    while (!g_stop) {
        /* Выставляем POLLOUT там, где есть что отправить. */
        for (size_t i = 1; i < g_nfds; ++i) {
            short ev = POLLIN;
            if (g_clients[i]->wlen > 0) ev |= POLLOUT;
            g_pfds[i].events = ev;
        }

        int n = poll(g_pfds, (nfds_t)g_nfds, 1000 /* ms */);
        if (n < 0) {
            if (errno == EINTR) continue;
            die("poll");
        }
        if (n == 0) continue;

        /* Сначала события на клиентах: идём с конца, чтобы безопасно
         * удалять (мы заменяем последним при удалении). */
        for (size_t i = g_nfds; i-- > 1; ) {
            short re = g_pfds[i].revents;
            if (re == 0) continue;
            client_t *c = g_clients[i];

            int drop = 0;
            if (re & (POLLERR | POLLNVAL)) drop = 1;
            if (!drop && (re & POLLIN))    drop = client_on_read(c);
            if (!drop && (re & POLLOUT))   drop = client_on_write(c);
            if (!drop && (re & POLLHUP) && c->wlen == 0) drop = 1;

            if (drop) client_remove(i);
        }

        /* Затем — новые подключения. */
        if (g_pfds[0].revents & POLLIN) accept_loop(lfd);
    }

    log_msg("STOP requested");
    return 0;
}
