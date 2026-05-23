#!/usr/bin/env bash
# runme.sh — сборка + полный прогон тестов + формирование result.txt.
#
# Этапы:
#   1. make — сборка bin/server, bin/client.
#   2. Запуск сервера в фоне.
#   3. Тест 1: 100 параллельных клиентов, проверка состояния = 0.
#   4. Тест 2: повторные запуски без перезапуска сервера + первая/последняя
#      записи fd/sbrk из лога.
#   5. Тест 3: эксперимент с разным N и задержкой; CSV с эффективностью.
#   6. Корректная остановка сервера, сборка result.txt.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

mkdir -p logs tests

SERVER_LOG=logs/server.log
RESULT=result.txt

# Чистый старт — старые логи могут запутать парсер.
rm -f "$SERVER_LOG" "$RESULT" logs/efficiency.txt config tests/*.stats 2>/dev/null || true
rm -rf tests/stats
mkdir -p tests/stats

echo "==> 1) Building"
make -B

echo "==> 2) Generating test data (1000 numbers, sum=0)"
./bin/gen_numbers tests/numbers.txt 1000

echo "==> 3) Starting server"
./bin/server -l "$SERVER_LOG" &
SERVER_PID=$!
trap 'kill -TERM '"$SERVER_PID"' 2>/dev/null || true; wait '"$SERVER_PID"' 2>/dev/null || true' EXIT
# Подождём, пока сервер реально начнёт слушать. config будет создан сервером.
for _ in $(seq 1 50); do
    [ -e "/tmp/brownian_bot.sock" ] && break
    sleep 0.1
done

echo "==> 4) Test 1: 100 concurrent clients, expect server state = 0"
./scripts/test_concurrent.sh 100 0.0005 | tee logs/test1.out

echo "==> 5) Test 2: repeated runs, no server restart"
./scripts/test_repeated.sh 3 30 0.0001 | tee logs/test2.out

echo "==> 6) Test 3: efficiency vs N and delay"
./scripts/test_efficiency.sh | tee logs/test3.out

echo "==> 7) Final check: server still alive and returns 0"
FINAL_REPLY=$(echo "0" | ./bin/client -c config)
echo "    final state: $FINAL_REPLY"

echo "==> 8) Stopping server"
kill -TERM "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
trap - EXIT

# ------------------------------------------------------------------------
# Формирование result.txt
# ------------------------------------------------------------------------

{
echo "================================================================"
echo "  RESULT REPORT — броуновский бот"
echo "  $(date '+%Y-%m-%d %H:%M:%S')"
echo "================================================================"
echo
echo "Описание тестов:"
echo
echo "  Test 1 (test_concurrent.sh):"
echo "    Запускаются 100 клиентов параллельно. Каждый читает один и"
echo "    тот же файл из 1000 чисел (сумма равна нулю) ПОБАЙТОВО, со"
echo "    случайными паузами после 1..255 байт. После завершения всех"
echo "    клиентов отправляется 0 и проверяется, что сервер вернёт 0."
echo "    Ожидаемый результат: 'server state after test: 0', OK."
echo
echo "  Test 2 (test_repeated.sh):"
echo "    Сценарий из Test 1 повторяется несколько раз без перезапуска"
echo "    сервера. Проверяется устойчивость к повторным сессиям."
echo "    Дополнительно из лога сервера извлекаются первая и последняя"
echo "    записи 'CONNECT fd=… sbrk=…' — это позволяет визуально"
echo "    оценить отсутствие лавинообразного роста fd и границы кучи."
echo
echo "  Test 3 (test_efficiency.sh):"
echo "    Запускается серия экспериментов с N клиентов и максимальной"
echo "    задержкой между байтами max_delay. Для каждой пары (N, delay)"
echo "    собираются метрики:"
echo "       wall_time     — от первого до последнего запроса по логам"
echo "       slowest_delay — суммарная задержка самого медленного клиента"
echo "       overhead      = wall_time − slowest_delay"
echo "    При корректной асинхронной реализации overhead должен быть мал"
echo "    и слабо расти с N — сервер не должен задерживать клиентов."
echo
echo "================================================================"
echo "  TEST 1: 100 concurrent clients"
echo "================================================================"
cat logs/test1.out
echo
echo "================================================================"
echo "  TEST 2: repeated runs without server restart"
echo "================================================================"
cat logs/test2.out
echo
echo "  ---- Первая и последняя записи 'CONNECT' (fd / sbrk) ----"
grep -m1 'CONNECT' "$SERVER_LOG" || true
grep 'CONNECT' "$SERVER_LOG" | tail -n1 || true
TOTAL_CONNECTS=$(grep -c 'CONNECT' "$SERVER_LOG" || true)
echo "  total CONNECT events: $TOTAL_CONNECTS"
echo
echo "================================================================"
echo "  TEST 3: efficiency vs N and delay"
echo "================================================================"
cat logs/test3.out
echo
echo "  ---- сводка из logs/efficiency.txt ----"
cat logs/efficiency.txt
echo
echo "================================================================"
echo "  Final state check"
echo "================================================================"
echo "  After all tests, server returned: $FINAL_REPLY"
if [ "$FINAL_REPLY" = "0" ]; then
    echo "  --> PASS"
else
    echo "  --> FAIL (expected 0)"
fi
echo
echo "================================================================"
echo "  Сводка"
echo "================================================================"
echo "  server log:        $SERVER_LOG"
echo "  efficiency table:  logs/efficiency.txt"
echo "  test outputs:      logs/test1.out, logs/test2.out, logs/test3.out"
} > "$RESULT"

echo
echo "==> Done. See $RESULT"
