<p align="center">
  <img src="logo_MIPT.png" alt="MIPT logo" width="300">
</p>

# Задачи по семинарам: классные и домашние работы

Репозиторий содержит решения и материалы по семинарам по курсу программирования на C (POSIX). Файлы организованы по занятиям (`Lesson_N`).

---

## Среда выполнения
- ОС: Linux/WSL/macOS (POSIX).
- Инструменты: `gcc` (или `clang`), `make`.

---

## Домашние задания и ссылки
| Lesson | Тема / краткое описание | Путь | Ссылка |
|:-----:|--------------------------|------|:-----:|
| 1 | `my_echo` — аналог `echo` (опция `-n`) | `Lesson_1/` | [описание](./Lesson_1/README.md) |
| 2 | Процессы и время: `measure_time`, `fork/wait`, цепочка процессов, `sleep-sort` | `Lesson_2/` | [описание](./Lesson_2/README.md) |
| 3 | Ввод/вывод: `my_cp` (`-f/-i/-v`), `my_cat`, тесты | `Lesson_3/` | [описание](./Lesson_3/README.md) |
| 4 | Pipes + мини-оболочка: `myshell`, `pipe_my_cat`, счётчик (wc-like) | `Lesson_4/` | [папка](./Lesson_4/) |
| 5 | «Стадион»/эстафета: SysV message queue vs POSIX mqueue | `Lesson_5/` | [SysV](./Lesson_5/lesson5_stadium.c)<br>[POSIX](./Lesson_5/lesson5_stadium_posix.c) |
| 6 | Unisex-душ на SysV семафорах (с бонус-логикой справедливости) | `Lesson_6/` | [код](./Lesson_6/lesson6_shower+bonus.c) |
| 7 | `cp` через `mmap` + «пиццерия» (потоки/семафоры) | `Lesson_7/` | [cp_mmap](./Lesson_7/lesson7_cp_mmap.c)<br>[pizza](./Lesson_7/lesson7_pizza.c) |
| 8 | Producer/consumer через `pthread_cond` (`pcat2`) + монитор Хоара (пицца) | `Lesson_8/` | [pcat2](./Lesson_8/lesson8_pcat2.c)<br>[pizza_hoare](./Lesson_8/lesson8_pizza_hoare.c) |
| 9 | «Сборка устройства»: 2 ключа + отвёртка (SysV sem + shm) | `Lesson_9/` | [код](./Lesson_9/lesson9_task3_test.c) |
| 10 | `myls` — упрощённый `ls` (`-l -i -n -R -a -d`, колонки, цвет) | `Lesson_10/` | [код](./Lesson_10/lesson10_myls.c) |
| 11 | Cигналы | `Lesson_11/` | [файл](./Lesson_11/lesson11_signal_cat.c) |
| 11 | Задача про богатырей | `Bogatyr/` | [файл](./Bogatyr/lesson13_bogatyr.c) |
| 11 | Финальная задача (зачёт) | `Final_test` | [файл](./Final_test/single.c) |

> В папках — исходники и (где есть) краткие инструкции/примеры запуска.

---

## Структура каталогов

```
Computer_Science/
  Lesson_1/
    lesson1_my_echo.c
  Lesson_2/
    lesson2_measure_time.c
    lesson2_parent.c
    lesson2_parent_follow.c
    lesson2_sort_time.c
  Lesson_3/
    lesson3_my_cat.c
    lesson3_my_cp.c
    test_my_cp.sh
  Lesson_4/
    lesson4_counter.c
    lesson4_myshell.c
    lesson4_pipe_my_cat.c
  Lesson_5/
    lesson5_stadium.c
    lesson5_stadium_posix.c
  Lesson_6/
    lesson6_shower+bonus.c
    README.md
  Lesson_6/
    lesson6_shower+bonus.c
  Lesson_7/
    lesson7_cp_mmap.c
    lesson7_pizza.c
  Lesson_8/
    lesson8_pcat2.c
    lesson8_pizza_hoare.c
  Lesson_9/
    lesson9_task3_test.c
  Lesson_10/
    lesson10_myls.c
  Lesson_11/
    lesson11_signal_cat.c
  Bogatyr/
    lesson13_bogatyr.c
  Final_test/
    single.c
```


