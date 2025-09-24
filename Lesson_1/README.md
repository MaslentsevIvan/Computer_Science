# Lesson 1 — `my_echo` (аналог `echo`)

**Назначение.** Выводит аргументы через пробел в одну строку. Поддерживает опцию `-n` (подавляет завершающий перевод строки). Обработки escape‑последовательностей нет.

## Сборка
```bash
gcc -std=c11 -Wall -Wextra -O2 lesson1_my_echo.c -o my_echo
```

## Примеры
```bash
./my_echo Hello world        # -> Hello world\n
./my_echo -n "no newline"    # -> no newline
```
