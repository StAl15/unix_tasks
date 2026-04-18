#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/types.h>
#include <time.h>

volatile sig_atomic_t sigint_received = 0;

void sigint_handler(int sig) {
    (void)sig; // для подавления предупреждения о неиспользуемом параметре
    sigint_received = 1;
}

int main(int argc, char *argv[]) {
    char *filename = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "f:")) != -1) {
        switch (opt) {
            case 'f':
                filename = optarg;
                break;
            default:
                fprintf(stderr, "Использование: %s -f имя_файла\n", argv[0]);
                exit(1);
        }
    }
    if (filename == NULL) {
        fprintf(stderr, "Имя файла обязательно\n");
        exit(1);
    }

    signal(SIGINT, sigint_handler);

    srand(time(NULL) + getpid()); // инициализация для случайных задержек

    int lock_count = 0;
    char lockfile[256];
    snprintf(lockfile, sizeof(lockfile), "%s.lck", filename);

    while (!sigint_received) {
        // начальная случайная задержка для балансировки
        usleep(rand() % 100000); // 0-0.1 сек
        // попытка создать файл блокировки
        int fd = open(lockfile, O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (fd == -1) {
            if (errno == EEXIST) {
                // ожидание с случайной задержкой для балансировки
                while (access(lockfile, F_OK) == 0 && !sigint_received) {
                    usleep(10000 + (rand() % 90000)); // случайная задержка 0.01-0.1 сек
                }
                // дополнительная случайная задержка перед повторной попыткой
                if (!sigint_received) {
                    usleep(rand() % 50000); // 0-0.05 сек
                }
                continue;
            } else {
                perror("открытие файла блокировки");
                exit(1);
            }
        }
        // запись pid
        pid_t pid = getpid();
        if (dprintf(fd, "%d\n", pid) < 0) {
            perror("запись pid");
            close(fd);
            exit(1);
        }
        close(fd);

        // работа с файлом
        int fd_file = open(filename, O_RDWR | O_CREAT, 0644);
        if (fd_file == -1) {
            perror("открытие файла");
            exit(1);
        }
        char buf[100];
        ssize_t n = read(fd_file, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            // модификация: добавление чего-то
            lseek(fd_file, 0, SEEK_END);
            write(fd_file, "modified\n", 9);
        }
        close(fd_file);

        // сон для наглядности
        sleep(1);

        // проверка файла блокировки
        struct stat st;
        if (stat(lockfile, &st) == 0) {
            // чтение pid
            FILE *fp = fopen(lockfile, "r");
            if (fp) {
                int read_pid;
                if (fscanf(fp, "%d", &read_pid) == 1 && read_pid == pid) {
                    if (unlink(lockfile) != 0) {
                        perror("удаление файла блокировки");
                        exit(1);
                    }
                    lock_count++;
                } else {
                    fprintf(stderr, "Файл блокировки поврежден или не наш\n");
                    exit(1);
                }
                fclose(fp);
            } else {
                perror("открытие файла блокировки для чтения");
                exit(1);
            }
        } else {
            fprintf(stderr, "Файл блокировки исчез\n");
            exit(1);
        }
    }

    // сохранение статистики
    FILE *stats = fopen("stats.txt", "a");
    if (stats) {
        fprintf(stats, "%d: %d\n", getpid(), lock_count);
        fclose(stats);
    } else {
        perror("открытие файла статистики");
    }

    return 0;
}