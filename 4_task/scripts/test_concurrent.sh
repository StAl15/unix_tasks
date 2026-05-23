#!/usr/bin/env bash
# test_concurrent.sh — Тест 1 из задания.
#
# Запускает N параллельных клиентов, каждый читает один и тот же файл
# с числами (сумма = 0) побайтово со случайными микро-задержками.
# По завершении всех клиентов отправляет на сервер "0" и проверяет,
# что ответ — тоже "0" (значит, состояние сервера обнулилось).
#
# Аргументы:
#   $1 — число клиентов (по умолчанию 100)
#   $2 — максимальная задержка между байтами (сек, по умолчанию 0.0005)
#   $3 — файл с числами

set -euo pipefail

N=${1:-100}
DELAY=${2:-0.0005}
NUMBERS=${3:-tests/numbers.txt}

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

mkdir -p logs tests/stats
rm -f tests/stats/*.stats

echo "[test_concurrent] запускаем $N клиентов, макс. задержка=$DELAY с"

PIDS=()
for i in $(seq 1 "$N"); do
    ./bin/client \
        -c config \
        -f "$NUMBERS" \
        -d "$DELAY" \
        -i "c$i" \
        -s "tests/stats/c$i.stats" \
        -q >/dev/null 2>&1 &
    PIDS+=($!)
done

# Дождаться всех клиентов.
FAILED=0
for pid in "${PIDS[@]}"; do
    if ! wait "$pid"; then
        FAILED=$((FAILED + 1))
    fi
done

if [ "$FAILED" -ne 0 ]; then
    echo "[test_concurrent] $FAILED клиентов завершились с ошибкой" >&2
    exit 1
fi
echo "[test_concurrent] все $N клиентов завершились"

# Контрольная проверка: отправим серверу 0 и убедимся, что в ответ — 0.
REPLY=$(echo "0" | ./bin/client -c config)
echo "[test_concurrent] состояние сервера после теста: $REPLY"

if [ "$REPLY" != "0" ]; then
    echo "[test_concurrent] ПРОВАЛ: ожидалось 0, получено '$REPLY'" >&2
    exit 1
fi

echo "[test_concurrent] ОК"
