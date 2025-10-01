# Lesson 4 — пайпы и мини‑оболочка (`myshell`)

**Тема.** Межпроцессное взаимодействие `pipe/dup2`, запуск процессов `fork/execvp`,
конвейеры `cmd1 | cmd2 | ...`, минимальная оболочка.

**Домашнее задание.** №2 — `myshell` (дата: **24.09.2025**).

## Состав папки
- `lesson4_myshell.c` — мини‑оболочка: поддержка конвейеров `|`, встроенные `exit`, `cd`, `pwd`,
  простая подстановка `~` в начале аргумента; `MYSHELL_DEBUG=1` включает отладочную печать.
- `lesson4_pipe_my_cat.c` — пример «читать файл/STDIN → передать в stdin внешней программе»
  (эквивалент `cat INPUT | PROGRAM ARGS...`).
- `lesson4_counter.c` — простой аналог `wc`: считает байты, слова, строки из входного потока.

## Сборка
```bash
gcc -std=c11 -Wall -Wextra -O2 lesson4_myshell.c -o myshell
gcc -std=c11 -Wall -Wextra -O2 lesson4_pipe_my_cat.c -o pipe_my_cat
gcc -std=c11 -Wall -Wextra -O2 lesson4_counter.c -o mywc
```

## Примеры
```bash
# myshell
./myshell
myshell$ echo a b c | wc -l
myshell$ cd ~
myshell$ pwd
myshell$ exit

# pipe_my_cat (чтение файла → wc)
./pipe_my_cat input.txt wc -l

# pipe_my_cat (STDIN → sort)
printf "b\na\nc\n" | ./pipe_my_cat - sort

# mywc
printf 'x y z\nqq\n' | ./mywc
./mywc < some.txt
```
