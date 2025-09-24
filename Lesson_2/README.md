# Lesson 2 — процессы и время

## Файлы и назначение
- **`lesson2_measure_time.c`** — запускает произвольную команду (`execvp(argv[1], &argv[1])`) и измеряет *реальное* время её выполнения по `gettimeofday` (обёртка вокруг `waitpid`).  
- **`lesson2_parent.c`** — родитель создаёт `N=3` дочерних процессов (`fork`), каждый печатает свой PID и завершает работу; родитель ждёт всех (`wait`).  
- **`lesson2_parent_follow.c`** — каскадная цепочка процессов 0…N: каждый порождает следующего; демонстрация родитель‑потомок и ожидания (см. `wait`/`waitpid`).  
- **`lesson2_sort_time.c`** — «sleep‑sort»: для каждого числа из аргументов порождается дочерний процесс, который `sleep(<число>)` и печатает его — числа выводятся по возрастанию времени сна.

## Сборка (примеры)
```bash
gcc -std=c11 -Wall -Wextra -O2 lesson2_measure_time.c -o measure_time
gcc -std=c11 -Wall -Wextra -O2 lesson2_parent.c -o parent
gcc -std=c11 -Wall -Wextra -O2 lesson2_parent_follow.c -o parent_follow
gcc -std=c11 -Wall -Wextra -O2 lesson2_sort_time.c -o sleep_sort
```

## Примеры запуска
```bash
# измерение времени внешней команды
./measure_time /bin/ls -l

# базовая работа с потомками
./parent

# цепочка родитель→потомок
./parent_follow

# sleep-sort
./sleep_sort 3 1 2 1
```
