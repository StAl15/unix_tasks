/*
 * gen_numbers.c — генератор файла с N целыми числами, сумма которых равна нулю.
 *
 * Использование:
 *   gen_numbers OUTPUT [N]
 *
 * Каждое число записывается на отдельной строке.
 * Сумма всех чисел строго равна нулю: первые N/2 — случайные значения,
 * вторые N/2 — их отрицания.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define LIMIT 10000000   /* 10^7 */

int main(int argc, char **argv)
{
    const char *outpath = (argc > 1) ? argv[1] : "tests/numbers.txt";
    long n = 1000;

    if (argc > 2) {
        char *end;
        n = strtol(argv[2], &end, 10);
        if (*end != '\0') {
            fprintf(stderr, "gen_numbers: некорректное число: %s\n", argv[2]);
            return EXIT_FAILURE;
        }
    }

    if (n < 2) {
        fprintf(stderr, "gen_numbers: N должно быть >= 2, получено %ld\n", n);
        return EXIT_FAILURE;
    }
    if (n % 2 != 0) {
        fprintf(stderr, "gen_numbers: N нечётное (%ld), округляем до %ld\n", n, n + 1);
        n++;
    }

    long *nums = malloc((size_t)n * sizeof(long));
    if (!nums) {
        fprintf(stderr, "gen_numbers: ошибка выделения памяти\n");
        return EXIT_FAILURE;
    }

    srand((unsigned)time(NULL));

    for (long i = 0; i < n / 2; ++i) {
        nums[i]       = (long)((double)rand() / ((double)RAND_MAX + 1) * (LIMIT + 1));
        nums[n/2 + i] = -nums[i];
    }

    FILE *f = fopen(outpath, "w");
    if (!f) {
        perror(outpath);
        free(nums);
        return EXIT_FAILURE;
    }
    for (long i = 0; i < n; ++i)
        fprintf(f, "%ld\n", nums[i]);
    fclose(f);

    printf("записано %ld чисел в %s, сумма=0\n", n, outpath);

    free(nums);
    return 0;
}
