/*
 * create_test_file.c — создание тестового файла для проверки работы
 * программы создания разреженных файлов.
 *
 * Создаёт бинарный файл заданного размера, заполненный нулевыми байтами,
 * с единицами (0x01) по трём фиксированным смещениям:
 *   - offset 0       — первый байт файла
 *   - offset 10000   — середина первого мегабайта
 *   - offset SIZE-1  — последний байт файла
 *
 * Такая структура удобна для тестирования: программа myprogram должна
 * создать разреженный файл с тремя маленькими «островками» данных
 * посреди огромных нулевых областей.
 *
 * Использование:
 *   ./create_test_file [имя_файла]
 *
 *   Если имя файла не указано, используется "fileA".
 */

#define _POSIX_C_SOURCE 200809L  /* ftruncate, pwrite */

#include <stdio.h>     /* fprintf, stderr, perror        */
#include <stdlib.h>    /* exit, EXIT_FAILURE, EXIT_SUCCESS */
#include <fcntl.h>     /* open, O_WRONLY, O_CREAT, O_TRUNC */
#include <unistd.h>    /* write, close, ftruncate, pwrite  */
#include <string.h>    /* strerror                         */
#include <errno.h>     /* errno                            */
#include <sys/types.h> /* off_t                            */

/*
 * Логический размер создаваемого тестового файла: 4 МиБ + 1 байт.
 * Значение не кратно степени двойки намеренно — это позволяет проверить,
 * что программа myprogram корректно обрабатывает последний неполный блок.
 */
#define FILE_SIZE  ((off_t)(4 * 1024 * 1024 + 1))

/*
 * Смещения, по которым записывается ненулевой байт (0x01).
 * OFFSET_MID выбрано так, чтобы попасть в середину первого мегабайта —
 * достаточно далеко от начала, чтобы между OFFSET_START и OFFSET_MID
 * образовалось несколько нулевых блоков по 4096 байт.
 */
#define OFFSET_START  ((off_t)0)
#define OFFSET_MID    ((off_t)10000)


int main(int argc, char *argv[]) {
    /*
     * Определяем имя выходного файла.
     * Если передан аргумент командной строки — используем его,
     * иначе берём имя по умолчанию "fileA".
     */
    const char *filename = (argc > 1) ? argv[1] : "fileA";

    /*
     * Маркерный байт, записываемый по трём смещениям.
     * Объявлен как переменная (а не макрос), чтобы можно было взять адрес
     * для передачи в pwrite().
     */
    const unsigned char marker = 1;

    /*
     * Открываем (или создаём) выходной файл:
     *   O_WRONLY — только для записи
     *   O_CREAT  — создать, если не существует
     *   O_TRUNC  — обнулить содержимое существующего файла
     *   0644     — права: владелец rw, группа r, остальные r
     */
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "Ошибка создания файла '%s': %s\n", filename, strerror(errno));
        exit(EXIT_FAILURE);
    }

    /*
     * Устанавливаем логический размер файла через ftruncate().
     * После этого вызова файл имеет размер FILE_SIZE байт, полностью
     * заполненных нулями (ядро не выделяет реальные дисковые блоки
     * под нулевые области — получается разреженный файл с одной большой дырой).
     * Это эффективнее, чем записывать FILE_SIZE нулевых байт вручную.
     */
    if (ftruncate(fd, FILE_SIZE) < 0) {
        fprintf(stderr, "Ошибка установки размера файла '%s': %s\n", filename, strerror(errno));
        close(fd);
        exit(EXIT_FAILURE);
    }

    /*
     * Записываем маркерный байт (0x01) по трём заданным смещениям.
     * Используем pwrite() — он записывает данные по явно указанному смещению,
     * не изменяя текущую позицию файлового дескриптора, и не требует
     * предварительного lseek(). Это атомарнее и нагляднее.
     *
     * pwrite() возвращает количество записанных байт (ожидаем 1)
     * или -1 при ошибке.
     */

    /* Маркер в начале файла — смещение 0 */
    if (pwrite(fd, &marker, 1, OFFSET_START) != 1) {
        fprintf(stderr, "Ошибка записи маркера по смещению %lld: %s\n",
                (long long)OFFSET_START, strerror(errno));
        close(fd);
        exit(EXIT_FAILURE);
    }

    /* Маркер в середине — смещение 10000 */
    if (pwrite(fd, &marker, 1, OFFSET_MID) != 1) {
        fprintf(stderr, "Ошибка записи маркера по смещению %lld: %s\n",
                (long long)OFFSET_MID, strerror(errno));
        close(fd);
        exit(EXIT_FAILURE);
    }

    /* Маркер в конце — последний байт файла, смещение FILE_SIZE - 1 */
    if (pwrite(fd, &marker, 1, FILE_SIZE - 1) != 1) {
        fprintf(stderr, "Ошибка записи маркера по смещению %lld: %s\n",
                (long long)(FILE_SIZE - 1), strerror(errno));
        close(fd);
        exit(EXIT_FAILURE);
    }

    close(fd);

    printf("Создан файл '%s': %lld байт, единицы по смещениям %lld, %lld, %lld\n",
           filename,
           (long long)FILE_SIZE,
           (long long)OFFSET_START,
           (long long)OFFSET_MID,
           (long long)(FILE_SIZE - 1));

    return EXIT_SUCCESS;
}