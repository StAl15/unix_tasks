#!/bin/bash

make clean
make

# создать общий файл
touch sharedfile

# запустить 10 процессов
pids=()
for i in {1..10}; do
    ./myprogram -f sharedfile &
    pids+=($!)
done

# ждать 5 минут
sleep 300

# отправить SIGINT всем
for pid in "${pids[@]}"; do
    kill -INT $pid 2>/dev/null
done

# ждать завершения
wait

# generate report
echo "Отчет о тесте" > result.txt
echo "===========" >> result.txt
echo "" >> result.txt
echo "Описание: 10 процессов работали параллельно в течение 5 минут, каждый пытаясь заблокировать 'sharedfile' на 1 секунду в цикле с случайными задержками для балансировки." >> result.txt
echo "Ожидаемый результат: Каждый процесс должен выполнить примерно 30 блокировок (300 секунд / 10 процессов / 1 секунда на цикл блокировки)." >> result.txt
echo "Количество блокировок должно быть примерно равным между процессами, что указывает на отсутствие тупиков или голодания." >> result.txt
echo "" >> result.txt
echo "Фактические результаты:" >> result.txt
if [ -f stats.txt ]; then
    cat stats.txt >> result.txt
    echo "" >> result.txt
    echo "Количество строк в stats.txt: $(wc -l < stats.txt)" >> result.txt
else
    echo "stats.txt не найден" >> result.txt
fi