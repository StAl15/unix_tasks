#!/usr/bin/env bash
# test_repeated.sh — Тест 2 из задания.
#
# 1. Запускает тестовый сценарий несколько раз подряд (без перезапуска
#    сервера) и проверяет, что состояние сервера каждый раз остаётся 0.
# 2. Извлекает из лога сервера первую и последнюю запись типа CONNECT,
#    в которых сохранены fd и значение sbrk(0), и показывает их —
#    это даёт грубую оценку отсутствия утечек дескрипторов и роста кучи.

set -euo pipefail

ROUNDS=${1:-3}
N=${2:-50}
DELAY=${3:-0.0001}
LOG=${4:-logs/server.log}

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "[test_repeated] $ROUNDS раундов × $N клиентов (задержка $DELAY с)"

for r in $(seq 1 "$ROUNDS"); do
    echo "[test_repeated] раунд $r/$ROUNDS"
    ./scripts/test_concurrent.sh "$N" "$DELAY"
done

echo
echo "[test_repeated] снимки ресурсов из лога сервера:"

# Первая и последняя записи CONNECT — содержат fd и sbrk.
FIRST=$(grep -m1 '^[0-9.]\+ CONNECT' "$LOG" || true)
LAST=$(grep '^[0-9.]\+ CONNECT' "$LOG" | tail -n1 || true)

if [ -z "$FIRST" ] || [ -z "$LAST" ]; then
    echo "[test_repeated] записей CONNECT в логе $LOG не найдено" >&2
    exit 1
fi

echo "  первый CONNECT: $FIRST"
echo "  последний CONNECT: $LAST"

# Сообщим, сколько всего подключений было.
TOTAL=$(grep -c '^[0-9.]\+ CONNECT' "$LOG" || true)
echo "  всего подключений: $TOTAL"

echo "[test_repeated] ОК"
