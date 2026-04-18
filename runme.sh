#!/usr/bin/env bash
# runme.sh — собирает программу создания разреженных файлов,
#            запускает все тесты и сохраняет отчёт в result.txt

set -euo pipefail

RESULT="result.txt"
PROG="./myprogram"

# определение ОС для совместимости stat(1)
# Linux: stat --printf="%s" file
# macOS: stat -f "%z" file  (размер) и stat -f "%b" file (блоки)
if stat --printf="%s" /dev/null >/dev/null 2>&1; then
    OS=linux
else
    OS=macos
fi

# вспомогательные функции

log()  { echo "$*" | tee -a "$RESULT"; }
sep()  { log ""; log "$(printf '─%.0s' {1..60})"; }

pass() { log "  [PASS] $*"; }
fail() { log "  [FAIL] $*"; }

check_equal() {
    local desc="$1" expected="$2" actual="$3"
    if [ "$expected" = "$actual" ]; then
        pass "$desc"
    else
        fail "$desc  (ожидалось='$expected'  получено='$actual')"
    fi
}

# Логический размер файла в байтах
logical_size() {
    if [ "$OS" = linux ]; then
        stat --printf="%s" "$1"
    else
        stat -f "%z" "$1"
    fi
}

# Реальный размер на диске в байтах (блоки * 512)
disk_usage() {
    local blocks
    if [ "$OS" = linux ]; then
        blocks=$(stat --printf="%b" "$1")
    else
        blocks=$(stat -f "%b" "$1")
    fi
    echo $(( blocks * 512 ))
}

# Вывод stat в человекочитаемом виде
stat_verbose() {
    if [ "$OS" = linux ]; then
        stat "$1"
    else
        stat -x "$1"
    fi
}

# очистка результатов предыдущего запуска
rm -f fileA fileB fileC fileD fileA.gz fileB.gz "$RESULT"

# заголовок отчёта
{
echo "========================================================"
echo "  Программа создания разреженных файлов — отчёт о тестах"
echo "  $(date)  [$OS]"
echo "========================================================"
} | tee "$RESULT"

# Шаг 0: сборка
sep
log "Шаг 0 — Сборка"
log "  Команда: make"
make 2>&1 | tee -a "$RESULT"
log "  Сборка завершена."

# Шаг 1: создание тестового файла A
sep
log "Шаг 1 — Создание тестового файла A"
log "  Описание  : 4*1024*1024+1 байт; значение 1 по смещениям 0, 10000 и в конце."
log "  Ожидаемый результат: файл успешно создан, логический размер = 4194305 байт."
log "  Команда   : ./create_test_file fileA"
./create_test_file fileA 2>&1 | tee -a "$RESULT"

EXPECTED_SIZE=$(( 4 * 1024 * 1024 + 1 ))
ACTUAL_SIZE=$(logical_size fileA)
check_equal "логический размер fileA = $EXPECTED_SIZE байт" "$EXPECTED_SIZE" "$ACTUAL_SIZE"

# Шаг 2: копирование A → B (разреженный файл)
sep
log "Шаг 2 — Копирование fileA в fileB (разреженный файл)"
log "  Описание  : Программа должна создать разреженную копию; реальный размер"
log "              на диске должен быть значительно меньше логического."
log "  Ожидаемый результат: fileB побайтово совпадает с fileA; fileB разрежен."
log "  Команда   : $PROG fileA fileB"
$PROG fileA fileB 2>&1 | tee -a "$RESULT"

SIZE_B=$(logical_size fileB)
DISK_B=$(disk_usage fileB)
log "  Логический размер fileB : $SIZE_B байт"
log "  Реальный размер fileB   : $DISK_B байт"
check_equal "логический размер fileB = логический размер fileA" "$ACTUAL_SIZE" "$SIZE_B"

if [ "$DISK_B" -lt "$ACTUAL_SIZE" ]; then
    pass "реальный размер fileB ($DISK_B) < логического ($ACTUAL_SIZE) — файл разрежен"
else
    log "  ПРИМЕЧАНИЕ: реальный размер fileB ($DISK_B) >= логического ($ACTUAL_SIZE)."
    log "              Это ожидаемо на ФС без поддержки разреженных файлов"
    log "              (например, overlayfs в Docker). На ext4/xfs/btrfs/APFS fileB БУДЕТ разрежен."
    pass "проверка разреженности fileB пропущена (ФС не поддерживает дыры)"
fi

if cmp -s fileA fileB; then
    pass "fileA и fileB побайтово идентичны"
else
    fail "fileA и fileB различаются!"
fi

# Шаг 3: сжатие A и B с помощью gzip
sep
log "Шаг 3 — Сжатие fileA и fileB с помощью gzip"
log "  Описание  : gzip эффективно сжимает длинные последовательности нулей,"
log "              поэтому размеры A.gz и B.gz ожидаются сопоставимыми."
log "  Ожидаемый результат: fileB.gz не больше fileA.gz по размеру."
log "  Команды   : gzip -k fileA  &&  gzip -k fileB"
gzip -k fileA 2>&1 | tee -a "$RESULT"
gzip -k fileB 2>&1 | tee -a "$RESULT"

SIZE_AGZ=$(logical_size fileA.gz)
SIZE_BGZ=$(logical_size fileB.gz)
log "  Размер fileA.gz : $SIZE_AGZ байт"
log "  Размер fileB.gz : $SIZE_BGZ байт"

if [ "$SIZE_BGZ" -le "$SIZE_AGZ" ]; then
    pass "fileB.gz ($SIZE_BGZ) <= fileA.gz ($SIZE_AGZ)"
else
    fail "fileB.gz ($SIZE_BGZ) больше fileA.gz ($SIZE_AGZ) — неожиданный результат"
fi

# Шаг 4: распаковка B.gz → stdout → разреженный файл C
sep
log "Шаг 4 — Распаковка fileB.gz через pipe в fileC (разреженный файл)"
log "  Описание  : gzip выдаёт обычный поток с нулями; программа должна"
log "              восстановить дыры в fileC при записи."
log "  Ожидаемый результат: fileC побайтово совпадает с fileA; fileC разрежен."
log "  Команда   : gzip -cd fileB.gz | $PROG fileC"
gzip -cd fileB.gz | $PROG fileC 2>&1 | tee -a "$RESULT"

SIZE_C=$(logical_size fileC)
DISK_C=$(disk_usage fileC)
log "  Логический размер fileC : $SIZE_C байт"
log "  Реальный размер fileC   : $DISK_C байт"
check_equal "логический размер fileC = логический размер fileA" "$ACTUAL_SIZE" "$SIZE_C"

if [ "$DISK_C" -lt "$ACTUAL_SIZE" ]; then
    pass "реальный размер fileC ($DISK_C) < логического ($ACTUAL_SIZE) — файл разрежен"
else
    log "  ПРИМЕЧАНИЕ: реальный размер fileC ($DISK_C) >= логического ($ACTUAL_SIZE)."
    log "              Это ожидаемо на ФС без поддержки разреженных файлов."
    pass "проверка разреженности fileC пропущена (ФС не поддерживает дыры)"
fi

if cmp -s fileA fileC; then
    pass "fileA и fileC побайтово идентичны"
else
    fail "fileA и fileC различаются!"
fi

# Шаг 5: копирование A → D с нестандартным размером блока (100 байт)
sep
log "Шаг 5 — Копирование fileA в fileD с размером блока 100 байт"
log "  Описание  : При блоке 100 байт блоки, содержащие единственный ненулевой"
log "              байт (смещения 0 и 10000), не будут полностью нулевыми и"
log "              будут записаны на диск. Логический размер должен совпадать с fileA."
log "  Ожидаемый результат: fileD побайтово совпадает с fileA."
log "  Команда   : $PROG -b 100 fileA fileD"
$PROG -b 100 fileA fileD 2>&1 | tee -a "$RESULT"

SIZE_D=$(logical_size fileD)
DISK_D=$(disk_usage fileD)
log "  Логический размер fileD : $SIZE_D байт"
log "  Реальный размер fileD   : $DISK_D байт"
check_equal "логический размер fileD = логический размер fileA" "$ACTUAL_SIZE" "$SIZE_D"

if cmp -s fileA fileD; then
    pass "fileA и fileD побайтово идентичны"
else
    fail "fileA и fileD различаются!"
fi

# Шаг 6: вывод stat для всех файлов
sep
log "Шаг 6 — Вывод stat для всех файлов"
log "  Описание  : Сравнение логического размера (Size) и реального использования"
log "              диска (Blocks) позволяет убедиться, какие файлы действительно"
log "              разрежены. На ФС с поддержкой дыр Blocks у fileB и fileC"
log "              должны быть значительно меньше, чем у fileA."
log ""
for f in fileA fileA.gz fileB fileB.gz fileC fileD; do
    if [ -e "$f" ]; then
        log "  ── $f"
        stat_verbose "$f" 2>&1 | tee -a "$RESULT"
        log ""
    else
        log "  ── $f  [ФАЙЛ НЕ НАЙДЕН]"
    fi
done

# итог
sep
PASS_COUNT=$(grep -c '^\s*\[PASS\]' "$RESULT" || true)
FAIL_COUNT=$(grep -c '^\s*\[FAIL\]' "$RESULT" || true)
log ""
log "Итого: пройдено — $PASS_COUNT, провалено — $FAIL_COUNT"
log "Полный отчёт сохранён в: $RESULT"

if [ "$FAIL_COUNT" -gt 0 ]; then
    exit 1
fi