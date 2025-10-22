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
| №   | Дата       | Тема / краткое описание                                          | Путь         | Ссылка |
|-----|------------|-------------------------------------------------------------------|--------------|:------:|
| 1   | 17.09.25   | `my_cp` — учебная реализация утилиты `cp` (`-f/-i/-v`)           | `Lesson_3/`  | [описание](./Lesson_3/README.md) |
| 2+3 | 24.09.25 + 01.10.25 | Мини‑оболочка `myshell`                                  | `Lesson_4/`  | [описание](./Lesson_4/README.md) |
| 4   | 08.10.25   | Unisex‑душ на SysV семафорах (`lesson6_shower+bonus.c`)           | `Lesson_6/`  | [описание](./Lesson_6/README.md) |

> В каждом разделе есть примеры сборки и краткие тесты.

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
```