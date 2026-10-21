# Регистрация игр с SSD на PS4 (Windows). Правый клик > Выполнить с помощью PowerShell.
# Нужно: PS4 включена, GoldHEN загружен, BinLoader включён, SSD подключён (папка PKG).
Clear-Host
Write-Host @"

    _         _      ____ ____  ____
   / \   __  _(_)___ / ___/ ___||  _ \
  / _ \  \ \/ / / __|\___ \___ \| | | |
 / ___ \  >  <| \__ \ ___) |__) | |_| |
/_/   \_\/_/\_\_|___/|____/____/|____/
                  by n0tkaXD

"@ -ForegroundColor Cyan

$ipFile = Join-Path $PSScriptRoot "ps4_ip.txt"
$ps4 = (Get-Content $ipFile -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch '^\s*(#|$)' } | Select-Object -First 1)
if ($ps4) { $ps4 = $ps4.Trim(); Write-Host "IP PS4 из ps4_ip.txt: $ps4" }
else {
    $ps4 = (Read-Host "Введите IP вашей PS4 (запомню в ps4_ip.txt)").Trim()
    Add-Content -Path $ipFile -Value $ps4
}
$payload = Join-Path $PSScriptRoot "proto\build\ifusb_scan.bin"

Write-Host "`n== Проверяю PS4 и папку PKG..."
$list = curl.exe -s --max-time 15 --list-only "ftp://$ps4:2121/mnt/usb0/PKG/"
if (-not $list) { Write-Host "!! PS4 не отвечает (включена? загружен GoldHEN? SSD подключён?)" -ForegroundColor Red; pause; exit 1 }
Write-Host "Игры в папке PKG:`n$list"

Write-Host "`n== Подключаюсь к логу PS4..."
$klog = New-Object System.Net.Sockets.TcpClient($ps4, 3232)
$reader = New-Object System.IO.StreamReader($klog.GetStream())

Write-Host "== Отправляю регистрацию..."
try {
    $bin = [System.IO.File]::ReadAllBytes($payload)
    $tcp = New-Object System.Net.Sockets.TcpClient($ps4, 9090)
    $tcp.GetStream().Write($bin, 0, $bin.Length)
    $tcp.Close()
} catch {
    Write-Host "!! Не отправилось. Включите BinLoader Server в настройках GoldHEN." -ForegroundColor Red
    $klog.Close(); pause; exit 1
}

Write-Host "== Жду результат (до 60 с)..."
$done = $false
$sw = [Diagnostics.Stopwatch]::StartNew()
while (-not $done -and $sw.Elapsed.TotalSeconds -lt 60) {
    if ($klog.GetStream().DataAvailable) {
        $line = $reader.ReadLine()
        if ($line -match "\[ifusb\]") { Write-Host ("  " + ($line -replace '.*\[ifusb\]\s*', '')) }
        if ($line -match "done:") { $done = $true }
    } else { Start-Sleep -Milliseconds 300 }
}
$klog.Close()
Write-Host ""
if ($done) { Write-Host "== Готово. Перезагрузите PS4 (GoldHEN загрузите снова) — игры появятся в меню." -ForegroundColor Green }
else { Write-Host "== Результат не получен. Проверьте строки выше." -ForegroundColor Yellow }
pause
