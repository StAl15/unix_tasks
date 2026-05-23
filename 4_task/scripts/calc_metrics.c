/*
 * calc_metrics.c — читает .stats-файлы из указанной директории,
 * вычисляет метрики эффективности и дописывает строку в .txt-файл.
 *
 * Использование:
 *   calc_metrics <n_clients> <max_delay> <out.txt> <stats_dir>
 *
 * Формат .stats-файла: строки вида key=value.
 * Нужные ключи: t_first, t_last, total_delay.
 *
 * Формат строки в out.txt:
 *   n=<n> delay=<d> wall=<wall> slowest=<slowest> overhead=<overhead>
 */

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PATH 1024
#define MAX_LINE  256

/* Парсит один .stats-файл */
static void process_stats(const char *path,
                          double *t_first, double *t_last, double *slowest)
{
    FILE *f = fopen(path, "r");
    if (!f) return;

    double file_tf = NAN, file_tl = NAN, file_td = 0.0;
    char line[MAX_LINE];

    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        if (strcmp(key, "t_first") == 0)     file_tf = strtod(val, NULL);
        else if (strcmp(key, "t_last") == 0) file_tl = strtod(val, NULL);
        else if (strcmp(key, "total_delay") == 0) file_td = strtod(val, NULL);
    }
    fclose(f);

    if (isnan(file_tf)) return;

    if (isnan(*t_first) || file_tf < *t_first) *t_first = file_tf;
    if (isnan(*t_last)  || file_tl > *t_last)  *t_last  = file_tl;
    if (file_td > *slowest) *slowest = file_td;
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "использование: %s <n> <delay> <out.txt> <stats_dir>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    const char *n_str     = argv[1];
    const char *d_str     = argv[2];
    const char *out_path  = argv[3];
    const char *stats_dir = argv[4];

    double t_first = NAN, t_last = NAN, slowest = 0.0;

    DIR *dir = opendir(stats_dir);
    if (!dir) {
        perror(stats_dir);
        return EXIT_FAILURE;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (len < 6 || strcmp(name + len - 6, ".stats") != 0) continue;

        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s/%s", stats_dir, name);
        process_stats(path, &t_first, &t_last, &slowest);
    }
    closedir(dir);

    double wall     = isnan(t_first) ? 0.0 : t_last - t_first;
    double overhead = wall - slowest;

    FILE *out = fopen(out_path, "a");
    if (!out) {
        perror(out_path);
        return EXIT_FAILURE;
    }
    fprintf(out, "n=%s delay=%s wall=%.4f slowest=%.4f overhead=%.4f\n",
            n_str, d_str, wall, slowest, overhead);
    fclose(out);

    printf("  общее_время=%.4fс макс_задержка=%.4fс накладные=%.4fс\n",
           wall, slowest, overhead);

    return 0;
}
