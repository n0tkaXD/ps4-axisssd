#!/bin/bash
# Регистрация игр с SSD на PS4. Двойной клик в Finder.
# Нужно: PS4 включена, GoldHEN загружен, BinLoader включён, SSD подключён (папка PKG).
cd "$(dirname "$0")" || exit 1
clear
cat <<'BANNER'

    _         _      ____ ____  ____
   / \   __  _(_)___ / ___/ ___||  _ \
  / _ \  \ \/ / / __|\___ \___ \| | | |
 / ___ \  >  <| \__ \ ___) |__) | |_| |
/_/   \_\/_/\_\_|___/|____/____/|____/
                  by n0tkaXD
BANNER

IPFILE="ps4_ip.txt"
PS4=$(grep -vE '^\s*(#|$)' "$IPFILE" 2>/dev/null | head -1 | tr -d ' \r')
if [ -n "$PS4" ]; then
    echo "IP PS4 из ps4_ip.txt: $PS4"
else
    read -r -p "Введите IP вашей PS4 (запомню в ps4_ip.txt): " PS4
    PS4=$(echo "$PS4" | tr -d ' \r')
    echo "$PS4" >> "$IPFILE"
fi
PAYLOAD=proto/build/ifusb_scan.bin

echo; echo "== Проверяю PS4 и папку PKG..."
LIST=$(curl -s --max-time 15 --list-only ftp://$PS4:2121/mnt/usb0/PKG/ 2>/dev/null)
if [ -z "$LIST" ] && ! curl -s --max-time 15 --list-only ftp://$PS4:2121/mnt/usb0/ >/dev/null 2>&1; then
    echo "!! PS4 не отвечает (включена? загружен GoldHEN? SSD подключён?)"; read -r -p "Нажмите Enter"; exit 1
fi
echo "Игры в папке PKG:"; echo "${LIST:-  (нет)}"

echo; echo "== Подключаюсь к логу PS4..."
LOG=$(mktemp)
nc $PS4 3232 > "$LOG" 2>/dev/null &
NCPID=$!
sleep 1

echo "== Отправляю регистрацию..."
if ! python3 -I tools/send_payload.py $PS4 9090 $PAYLOAD; then
    echo "!! Не отправилось. Включите BinLoader Server в настройках GoldHEN."
    kill $NCPID 2>/dev/null; rm -f "$LOG"; read -r -p "Нажмите Enter"; exit 1
fi

echo "== Жду результат (до 60 с)..."
for i in $(seq 1 30); do
    sleep 2
    grep -q "done:" "$LOG" 2>/dev/null && break
done
kill $NCPID 2>/dev/null
echo
strings "$LOG" | grep '\[ifusb\]' | sed 's/^.*\[ifusb\]/  /'
echo
if strings "$LOG" | grep -qE "[1-9][0-9]* registered|REGISTERED"; then
    echo "== Готово. Перезагрузите PS4 (GoldHEN загрузите снова) — игры появятся в меню."
else
    echo "== Новых игр не зарегистрировано. Проверьте строки выше."
fi
rm -f "$LOG"
read -r -p "Нажмите Enter, чтобы закрыть"
