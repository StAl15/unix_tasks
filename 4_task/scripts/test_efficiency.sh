#!/usr/bin/env bash
# test_efficiency.sh — Тест 3 из задания.
#
# Цель: проверить, что время работы сервера примерно равно времени
# выполнения самого "медленного" клиента — то есть сервер не вносит
# своих собственных задержек, а лишь обслуживает клиентов асинхронно.
#
# Эффективность считается так:
#   wall_time     = t_last_overall - t_first_overall
#   slowest_delay = max( total_delay по всем клиентам )
#   overhead      = wall_time - slowest_delay
#
# При корректной асинхронности overhead должен быть мал и слабо
# зависеть от N (числа клиентов).
#
# Скрипт сохраняет результаты в logs/efficiency.csv.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

TXT=logs/efficiency.txt
# Для эксперимента эффективности используется укороченный файл —
# при задержке до 1с и тысяче чисел тест занял бы часы. Файл маленький,
# но достаточный, чтобы измерить накладные расходы сервера.
NUMBERS=tests/numbers_small.txt
./bin/gen_numbers "$NUMBERS" 20 >/dev/null

# Сетки. Задание просит 1..100 клиентов и задержку 0..1 с шагом 0.2.
# Полный квадрат = 6 × несколько точек по N. Чтобы тест не шёл вечно,
# берём представительные точки по N: 1, 10, 50, 100.
N_LIST=(1 10 50 100)
DELAY_LIST=(0.0 0.2 0.4 0.6 0.8 1.0)

mkdir -p logs tests/stats
printf '' > "$TXT"

for D in "${DELAY_LIST[@]}"; do
    for N in "${N_LIST[@]}"; do
        echo "[efficiency] N=$N задержка=$D"
        rm -f tests/stats/*.stats

        # Запускаем N клиентов параллельно.
        PIDS=()
        for i in $(seq 1 "$N"); do
            ./bin/client \
                -c config \
                -f "$NUMBERS" \
                -d "$D" \
                -i "c$i" \
                -s "tests/stats/c$i.stats" \
                -q >/dev/null 2>&1 &
            PIDS+=($!)
        done
        for pid in "${PIDS[@]}"; do wait "$pid" || true; done

        # Считаем метрики на основе .stats-файлов.
        ./bin/calc_metrics "$N" "$D" "$TXT" tests/stats
    done
done

echo
echo "[efficiency] результаты сохранены в $TXT:"
cat "$TXT"
