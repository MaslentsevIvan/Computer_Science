#!/usr/bin/env bash
# test_my_cp.sh — краштесты для my_cp со сводкой PASS/FAIL
# Запуск: bash test_my_cp.sh /path/to/lesson3_my_cp

set -euo pipefail

# --- Абсолютный путь к бинарю ---
BIN_INPUT="${1:-./lesson3_my_cp}"
case "$BIN_INPUT" in
  /*) BIN="$BIN_INPUT" ;;
  *)  BIN="$(pwd)/$BIN_INPUT" ;;
esac

# --- учёт результатов ---
PASS=0; FAIL=0; TOTAL=0
ok(){ PASS=$((PASS+1)); TOTAL=$((TOTAL+1)); echo "✅ PASS"; }
bad(){ FAIL=$((FAIL+1)); TOTAL=$((TOTAL+1)); echo "❌ FAIL"; }
say(){ printf "\n== %s ==\n" "$*"; }

# --- универсальный раннер: лог + возврат кода ---
run_cmd() {
  local cmd="$1" rc
  echo "\$ $cmd"
  set +e
  ( set +e -o pipefail; eval "$cmd" )
  rc=$?
  set -e
  echo "[exit=$rc]"
  return "$rc"
}

expect_ok(){          # ожидаем exit==0
  if run_cmd "$*"; then ok; else bad; fi
}
expect_fail(){        # ожидаем exit!=0
  if run_cmd "$*"; then bad; else ok; fi
}
expect_diff_eq(){     # ожидаем равенство файлов по содержимому
  local a="$1" b="$2"
  if run_cmd "diff -q \"$a\" \"$b\" >/dev/null"; then ok; else bad; fi
}
expect_wc_bytes(){    # ожидаем размер файла в байтах
  local want="$1" f="$2" got
  echo "\$ wc -c $f | awk '{print \$1}' == $want"
  set +e; got=$(wc -c < "$f" 2>/dev/null); rc=$?; set -e
  echo "[$f: ${got:-?} bytes]"
  if [ "$rc" -eq 0 ] && [ "$got" = "$want" ]; then ok; else bad; fi
}

# --- Песочница ---
WORK="/tmp/mycp_lab"
rm -rf "$WORK" && mkdir -p "$WORK"
cd "$WORK"

# --- Фикстуры ---
printf 'hello\n' > file1.txt
printf 'world\n' > file2.txt
: > empty.txt
mkdir -p testdir
ln -sf file1.txt link1
mkfifo -m 644 p1
dd if=/dev/urandom of=bin.dat bs=1M count=1 status=none >/dev/null 2>&1 || true

say "Бинарь существует"
expect_ok "ls -l '$BIN' >/dev/null"

say "Базовые ошибки аргументов"
expect_fail "'$BIN'"
expect_fail "'$BIN' file1.txt"
expect_fail "'$BIN' no_such.txt testdir/"

say "DEST не директория при >=3 SRC"
expect_fail "'$BIN' file1.txt file2.txt notadir.txt"

say "Два аргумента: dest — директория"
expect_ok   "'$BIN' -v file1.txt testdir/"
expect_diff_eq file1.txt testdir/file1.txt

say "Несколько файлов -> директория"
expect_ok   "'$BIN' -v file1.txt file2.txt empty.txt testdir/"
expect_diff_eq file2.txt testdir/file2.txt
expect_wc_bytes 0 testdir/empty.txt

say "-i: отказ (оставляет старый файл)"
printf 'OLD\n' > testdir/file1.txt
expect_ok   "printf 'n\\n' | '$BIN' -v -i file1.txt testdir/"
# сравним со временным эталоном
printf 'OLD\n' > /tmp/expected_old.txt
expect_diff_eq /tmp/expected_old.txt testdir/file1.txt

say "-i: согласие (перезаписывает)"
expect_ok   "printf 'y\\n' | '$BIN' -v -i file1.txt testdir/"
expect_diff_eq file1.txt testdir/file1.txt

say "-f: перезапись без вопросов"
printf 'OLD\n' > testdir/file2.txt
expect_ok   "'$BIN' -v -f file2.txt testdir/"
expect_diff_eq file2.txt testdir/file2.txt

say "SRC==DEST (ошибка и НЕ обнуляем)"
cp file1.txt same.txt
expect_fail "'$BIN' -v same.txt same.txt"
expect_wc_bytes 6 same.txt

say "SRC — директория (ошибка без -r)"
expect_fail "'$BIN' testdir file2.txt"

say "Двойной слэш в пути (OK по POSIX)"
expect_ok   "'$BIN' -v file1.txt testdir//"

say "Пустой SRC-аргумент (ошибка)"
expect_fail "'$BIN' '' testdir/"

say "Длинное имя файла (~240 символов)"
LONGNAME="$(printf 'a%.0s' {1..240}).txt"
printf foo > "$LONGNAME"
expect_ok   "'$BIN' \"$LONGNAME\" testdir/"

say "Имена с пробелами и Юникодом"
printf 'привет\n' > "файл с пробелом.txt"
expect_ok   "'$BIN' 'файл с пробелом.txt' testdir/"
expect_diff_eq "файл с пробелом.txt" "testdir/файл с пробелом.txt"

say "Симлинк как SRC (копируется содержимое цели)"
expect_ok   "'$BIN' -v link1 testdir/"
expect_diff_eq file1.txt testdir/link1

say "FIFO как SRC (не зависаем, есть писатель)"
( sleep 0.1; printf 'fifo\n' > p1 ) &
expect_ok   "'$BIN' -v p1 testdir/"
printf 'fifo\n' > /tmp/expected_fifo.txt
expect_diff_eq /tmp/expected_fifo.txt testdir/p1

say "Бинарный файл (cmp == OK)"
expect_ok   "'$BIN' -v bin.dat testdir/"
expect_ok   "cmp -s bin.dat testdir/bin.dat"

say "Нечитаемый SRC"
cp file1.txt ro_src.txt && chmod 000 ro_src.txt
expect_fail "'$BIN' ro_src.txt testdir/"
chmod 644 ro_src.txt

say "Незаписываемая директория DEST"
mkdir -p ro_dir && chmod 555 ro_dir
expect_fail "'$BIN' file1.txt ro_dir/"
chmod 755 ro_dir

say "Длинные опции (--verbose/--interactive/--force)"
expect_ok   "'$BIN' --verbose --interactive file1.txt testdir/ <<<'y'"
expect_ok   "'$BIN' --force file2.txt testdir/"

say "Копирование самого бинаря"
expect_ok   "'$BIN' -v '$BIN' testdir/"

# --- сводка ---
echo
echo "===== SUMMARY ====="
echo "PASS: $PASS"
echo "FAIL: $FAIL"
echo "TOTAL: $TOTAL"
[ "$FAIL" -eq 0 ] || exit 1