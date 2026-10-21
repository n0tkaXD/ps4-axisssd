# «Install to USB» для форка Itemzflow — исследование и план

Дата: 2026-10-20. Исходники склонированы рядом:
- `Itemzflow/` — LightningMods/Itemzflow, коммит `9126e47` (2026-07-09)
- `ps4mount/` — gasb07/ps4mount, коммит `3de382d` (v0.6.0, 2026-10-16)
- `ps4-pkg2usb/` — SiSTR0/ps4-pkg2usb, коммит `f7338c7` (2018)

Обозначения: **[Ф]** — факт, подтверждён кодом/источником (ссылка рядом). **[Г]** — гипотеза, рядом написано, как проверить.

---

## 1. Краткий вывод

**Реализуемо, сложность средняя.** Основная работа — перенести на PS4 уже готовую логику ps4mount: сгенерировать файлы, которые оставляет установка, и попросить систему саму зарегистрировать игру. Нового кода ядра писать не нужно.

- **[Ф]** Перенаправление в Itemzflow — это обычный `symlink()`: `/user/app/<TID>/app.pkg` → `/mnt/usbN/itemzflow/apps/<TID>/app.pkg`. Никакого nullfs и никаких патчей ядра для этого не используется (`itemzflow/source/GLES2_itemzflow.cpp:1813-1819`).
- **[Ф]** То, что файл на USB — это исходный PKG, подтверждает README pkg2usb («copy the pkg to X:\PS4\CUSAxxxxx\app.pkg»). ps4mount на PS5 тоже ставит симлинк прямо на исходный `.pkg`.
- **[Ф]** ps4mount на PS5 регистрирует игру без копирования: генерирует служебные файлы и вызывает `sceAppInstUtilAppInstallAll` / `sceAppInstUtilAppInstallTitleDir`. **Сам в app.db не пишет**: по опыту автора, внешняя запись заставляет ShellCore пометить базу как повреждённую (`ps4mount/src/main.c:1232-1235`, `src/db.c:1-5`).
- **[Ф]** На PS4 обе функции регистрации существуют: они есть в `libSceAppInstUtil.sprx` для FW 9.00 ([idc/ps4libdoc, ветка 9.00](https://github.com/idc/ps4libdoc/blob/9.00/system/common/lib/libSceAppInstUtil.sprx.json)).
- **[Г]** Что они есть и так же работают на **12.00**, не проверено. Сигнатуры взяты из кода ps4mount для PS5. Проверка — эксперимент из раздела 6 и шаг 1 плана.
- Самое неизвестное: **какой именно набор файлов оставляет установка fPKG на PS4 12.00** (у ps4mount форматы сняты с PS5 13.20). Это закрывает один эксперимент «снимок до/после установки» (раздел 6).

Оценка трудоёмкости: прототип регистрации (отдельный payload) — несколько вечеров. Интеграция в Itemzflow — больше, в основном из-за сборки: SDK у Itemzflow нестандартный (раздел 3.4).

---

## 2. Как это устроено сейчас

### 2.1 Itemzflow — «Move to USB» [Ф]
`itemzflow/source/GLES2_itemzflow.cpp:1764-1843`
1. Проверяет наличие USB, что игра не на расширенном хранилище (`is_ext_hdd`) и свободное место.
2. `copyFile("/user/app/<TID>/app.pkg", "/mnt/usbN/itemzflow/apps/<TID>/app.pkg")`, затем сверяет MD5 (`MD5_file_compare`).
3. `unlink(src)` и `symlink(dst, src)`: перенаправление — это обычная символическая ссылка на файловой системе `/user`.
4. Копирует весь `/user/license` в `/mnt/usbN/itemzflow/licenses` (бэкап лицензий).
5. DLC: `/user/addcont/<TID>/ac.pkg` → `app_dlc.pkg`. Патч: `/user/patch/<TID>/patch.pkg` → `app_patch.pkg`. Механизм тот же.

Записи в app.db не меняются: игра остаётся «установленной», меняется только место хранения байтов `app.pkg`.

### 2.2 Itemzflow — «Restore from USB» [Ф]
`GLES2_itemzflow.cpp:1846-1910`: удаляет симлинк и вызывает обычную установку `pkginstall(<pkg на USB>, …)` (удаляет приложение и ставит заново). По issue [LightningMods/Itemzflow#12](https://github.com/LightningMods/Itemzflow/issues/12) автор сказал, что Restore уберут, а Move станет «билетом в один конец». «Rebuild DB» симлинкованные игры не чинит.

### 2.3 Itemzflow — PKG Installer [Ф]
`itemzflow/source/installpkg.cpp:295` `pkginstall()`:
`sceAppInstUtilGetTitleIdFromPkg` → заполняет `bgft_download_param_ex` (`entitlement_type=5`, `content_url=<локальный путь>`) → `sceBgftServiceIntDownloadRegisterTaskByStorageEx` → `sceBgftServiceDownloadStartTask`. Это системный установщик BGFT: он и копирует PKG на внутренний диск. Обойти копирование внутри BGFT нельзя, поэтому для «Install to USB» BGFT не подходит.

### 2.4 Itemzflow — ручная запись в app.db [Ф]
- `itemzflow/source/sql.cpp:411` `Fix_Game_In_DB()`: `INSERT OR IGNORE` в `tbl_appbrowse_0<userId>` (для каждого пользователя) и около 60 ключей в `tbl_appinfo` (`_org_path`, `_metadata_path`, `CONTENT_ID`, `CATEGORY`, `TITLE_xx`, `#_size` и т.д.) из param.sfo.
- Вызывается из «Rebuild DB» (`utils.cpp:2255` `rebuild_db()`) и как «лечение», если `sceLncUtilLaunchApp` вернул `APP_NOT_FOUND` или `NO_SFOKEY_IN_APP_INFO` (`utils.cpp:2042` `Launch_App`).
- Поле `#_size` берётся из `file_size(item.info.package)`, то есть ожидается существующий `app.pkg`.
- `sql.cpp:760` `Inject_SQL_app()` — обёртка, которая регистрирует «фейковое» приложение через тот же `Fix_Game_In_DB`.

Эту функцию можно использовать как запасной вариант регистрации, но ps4mount прямо предупреждает о риске «database corrupted» (см. 2.6).

### 2.5 Itemzflow — nullfs и «vapps» (только PS5) [Ф]
`ls_dir.cpp:63` `remount()` — `nmount("nullfs", fspath, target)`. Используется в `sql.cpp:653` `retrieveFVapps()` для PS5: папка с дампом игры монтируется в `/system_ex/app/<TID>`. Для PS4 fPKG это не нужно, но готовая обёртка nmount в коде есть.

### 2.6 ps4mount (PS5 + etaHEN) [Ф]
Файлы: `ps4mount/src/main.c`, `build.c`, `pkg.c`, `db.c`, `fw.c`.

1. **Раскладка на USB**: `<root>/ps4/<TID>/app.pkg` (+ `patch.pkg`, `addcont/…/ac.pkg`). Это `rename` на том же диске, без копирования.
2. **Генерация «следов установки»** — `place_files()` (`main.c:553`):
   - `/user/appmeta/<TID>/`: icon0.png, pic0/pic1, snd0.at9, *.dds, pronunciation.xml, shareparam.json, changeinfo/*. Берутся из записей PKG (`k_user_meta`, `main.c:181`).
   - `/system_data/priv/appmeta/<TID>/`: `param.sfo` (запись 0x1000), `nptitle.dat` (0x402), `npbind.dat` (0x403).
   - Лицензия: `/user/license/fake<CID[:19]>.rif` и `.idx`. Строится из записи `license.dat` (0x400), расшифрованной публичным fake-ключом (`place_license`, `main.c:467`; `build_license`, `build.c`; ключ — `keys.h` из LibOrbisPkg).
   - Папка игры: `app.json` (url → путь к pkg), `app.pbm` (+`.backup`), `app.xml` (статус PlayGo).
   - PlayGo: `/system_data/playgo/<TID>/pfs.pbm`, `/user/playgo/app/<CID>/00/d0.pdb`, `d1.pdb` (из шаблонов `tools/pdb_*_template.bin`).
   - Комментарий в `build.c`: «Formats were taken from real installs on FW 13.20» — это PS5.
3. **Перенаправление**: на PS5 `/user` — это bfs **без поддержки симлинков** (`main.c:53-55`). Поэтому служебная папка с симлинком `app.pkg → USB` лежит в `/system_data/ps4mount/app/<TID>` (ufs) и монтируется поверх `/user/app/<TID>` через **nullfs** (`nullfs_mount`, `main.c:692`; `mount_dir`, `main.c:758`).
4. **Регистрация — делает сама система** (`register_call`, `main.c:1009`):
   - FW ≥ 6.00: `sceAppInstUtilAppInstallAll(NULL)`. ShellCore обходит `/user/app` и добавляет новые тайтлы. Чтобы не перебирать все игры, на время вызова `/user/app` маскируется пустой stage-папкой с новыми играми (`mask_user_app`, `main.c:1163`).
   - FW < 6.00: `sceAppInstUtilAppInstallTitleDir(tid, "/user/app/", NULL)`, найденная через dlsym (`check_firmware`, `main.c:988`).
   - Результат проверяется только чтением `SELECT titleId FROM tbl_contentinfo` в режиме `mode=ro&nolock=1` (`db.c`).
5. **Патч**: после регистрации подменяет `/system_data/priv/appmeta/<TID>/param.sfo` на param.sfo апдейта, иначе запуск отклоняется («system file disagree») (`enforce_patch_sfo`, `main.c:954`).
6. **klog** (`handle_klog_line`, `main.c:2009`): следит за запуском, экранами удаления (чтобы вовремя размонтировать) и «system file disagree». Без klog удаление и повторная регистрация работают хуже.
7. **Ограничения из README**: перед режимом покоя игру нужно закрыть, иначе после пробуждения она зависнет; диск во время игры не вынимать; после каждой загрузки ps4mount нужно запускать заново.

### 2.7 ps4-pkg2usb (PS4 4.05/4.55) [Ф]
`source/link.c:251-256`: `unlink(/user/app/<TID>/app.pkg)` + `symlink(<usb>/PS4/<TID>/app.pkg, …)`. То же, что Move в Itemzflow. README: «always use the same USB port» — путь `/mnt/usbN` зашит в симлинк.

### Вывод из сравнения
| | Itemzflow Move | pkg2usb | ps4mount (PS5) |
|---|---|---|---|
| Регистрация | обычная установка (BGFT) | обычная установка | генерация файлов + `AppInstallAll`/`TitleDir` |
| Перенаправление | symlink в `/user/app` | symlink в `/user/app` | nullfs-папка с symlink поверх `/user/app/<TID>` |
| Запись на внутренний диск | весь PKG (потом удаляется) | весь PKG | только метаданные (КБ) |

Для PS4 нужна комбинация: **генерация файлов и регистрация как в ps4mount + перенаправление симлинком как в Itemzflow** (на PS4 `/user` — UFS, симлинки работают; доказательство — рабочий Move to USB).

---

## 3. Что нужно изменить в форке

### 3.1 Минимум «системной установки» на PS4

**[Ф] Обязательно на любой платформе (по коду ps4mount и Itemzflow):**
- `/user/app/<TID>/app.pkg` — симлинк на PKG на exFAT.
- `/user/appmeta/<TID>/param.sfo, icon0.png …` — иначе нет плитки и иконки (Itemzflow читает `metaDataPath` отсюда, `sql.cpp`).
- Строки в app.db (`tbl_contentinfo`, `tbl_appbrowse_0<user>`, `tbl_appinfo`). Без них `sceLncUtilLaunchApp` возвращает `APP_NOT_FOUND` (это видно по обработчику в `Launch_App`).

**[Г] Скорее всего нужно и на PS4 (на PS5 точно нужно), проверяется экспериментом из раздела 6:**
- Fake-лицензия `/user/license/fake<CID19>.rif/.idx`. Itemzflow при Move бэкапит весь `/user/license`, значит лицензии там есть. Есть ли у fPKG на PS4 именно `fake*.rif` и в каком формате — проверить.
- `/system_data/priv/appmeta/<TID>/{param.sfo,nptitle.dat,npbind.dat}`.
- `app.json`/`app.pbm`/`app.xml` в `/user/app/<TID>/` и записи PlayGo (`/user/playgo/…`, `/system_data/playgo/<TID>/pfs.pbm`). На PS4 их может не быть совсем или формат может отличаться.

**Можно ли зарегистрировать игру без записи данных на HDD?** Да, если считать «данными» сам PKG. Записывать придётся только метаданные (десятки–сотни КБ: иконки, sfo, лицензия, PlayGo). Сам PKG на внутренний диск не попадает.

### 3.2 Новый модуль `itemzflow/source/usb_install.cpp` (+ `include/usb_install.h`)
1. **Чтение PKG** — перенести из ps4mount `pkg.c`/`pkg.h`, `crypto.c` (SHA-256, AES-128-CBC, RSA-2048), `keys.h`. Лицензия у всех GPL-3.0, переносить можно. Сохранить копирайты.
   Не путать с уже имеющимся `struct pkg_header` в `include/installpkg.h`.
2. **Генерация файлов** — `build.c` из ps4mount. Использовать только те файлы, которые эксперимент подтвердит для PS4.
3. **Раскладка на USB**: `/mnt/usbN/PS4/<TID>/app.pkg` (совместимо с pkg2usb) или `/mnt/usbN/itemzflow/apps/<TID>/app.pkg` (совместимо с Move/Restore Itemzflow). Рекомендую второй вариант: тогда существующий «Restore from USB» будет работать как «поставить на внутренний диск».
4. **Перенаправление**: `mkdir /user/app/<TID>` + `symlink(<usb pkg>, "/user/app/<TID>/app.pkg")`, как в `GLES2_itemzflow.cpp:1818`.
5. **Регистрация**, в порядке предпочтения:
   - a. `sceAppInstUtilAppInstallTitleDir(tid, "/user/app/", NULL)`. Резолвить через `sceKernelLoadStartModule` + `sceKernelDlsym` во время работы, не линковать статически: так приложение не упадёт при старте, если символа нет. GoldHEN заявляет «sys_dynlib_dlsym Patch» ([релизы GoldHEN](https://github.com/GoldHEN/GoldHEN/releases)).
   - b. `sceAppInstUtilAppInstallAll(NULL)` — с маскировкой `/user/app`, как в ps4mount (`remount()` из `ls_dir.cpp` уже есть). Иначе ShellCore будет перебирать все игры, и неизвестно, как он отнесётся к уже установленным.
   - c. Запасной вариант: `Fix_Game_In_DB()` (прямая запись в app.db). Только после бэкапа базы и только если a/b недоступны.
   - Перед a/b вызвать `sceAppInstUtilInitialize()` (уже есть в `app_inst_util_init()`, `installpkg.cpp:34`). Вызов делать в отдельном потоке с тайм-аутом: в ps4mount `InstallAll` после режима покоя зависал навсегда (`main.c:1002-1004`).
6. **Проверка**: чтение `tbl_contentinfo` только с `?mode=ro&nolock=1` (как в `ps4mount/src/db.c`). Обычный `SQL_Load_DB` в Itemzflow открывает базу на запись — для проверки он не годится.
7. **Удаление** («Uninstall from USB»): `sceAppInstUtilAppUnInstall(tid)`. До этого сам удалить симлинк, чтобы деинсталлятор не пошёл по ссылке. **[Г]** Удаляет ли деинсталлятор файл, на который указывает ссылка: в `rm -rf`-логике FreeBSD unlink удаляет саму ссылку, но поведение ShellCore неизвестно. Проверить на мусорном PKG.

### 3.3 Интерфейс
- Новый пункт рядом с PKG Installer (`GLES2_menu.cpp`, `GLES2_itemzflow.cpp`): «Install to USB (no copy)» для `.pkg`, найденных на `/mnt/usbN`.
- Строки в `include/lang.h` + `App-Media-Assets/langs/*/lang.ini`.
- Проверки: GoldHEN загружен, USB смонтирован, ФС = exFAT (`statfs` → `f_fstypename`), игра ещё не установлена (`sceAppInstUtilAppExists`), PKG — именно fake (`pkg_fake_key()`).
- **Только base game** в первой версии. Патчи и DLC — потом (у ps4mount для них отдельная логика с `enforce_patch_sfo` и PlayGo patch).

### 3.4 Тулчейн и сборка на macOS [Ф + Г]
- **[Ф]** Makefile'ы (`itemzflow/Makefile`, `itemz-daemon/Makefile`, `itemz-loader/Makefile`) требуют `ORBISDEV` и `$(ORBISDEV)/make/ps4sdk.mk`. Это [orbisdev/orbisdev](https://github.com/orbisdev/orbisdev): toolchain, sdkvanilla, liborbis, pkgtool. Сборка: `make` → `orbis-elf-create` → `python2 make_fself.py` → `pkgTool pkg_build App-Media-Assets/itemzflow.gp4`. Порядок задаёт `itemzflow/build.sh`.
- **[Ф]** Есть Docker-образ `orbisdev/orbisdev:latest` (amd64, обновлён 2024-05-19). Твой Mac — Intel x86_64 (`uname -m`), так что образ пойдёт без эмуляции. Docker на Mac сейчас не установлен — нужен Docker Desktop или colima.
- **[Ф]** Линкуются библиотеки, которых **нет** в стандартном `orbisdev-liborbis` (`build.sh` там собирает: utils, libkernelUtil, libdebugnet, libz, liborbisPad, liborbisAudio, liborbisNfs, liborbisKeyboard, libmod, libSQLite, liborbisLink). Отсутствуют: `-lorbisGl`, `-lfreetype-gl`, `-lorbis`, `-luser_mem_sys`, `-lyaml-cpp` (в `build.sh` есть `../libyaml-cpp`, в репо этой папки нет), а также часть стабов (`ScePigletv2VSH`, `SceShellCoreUtil`, `SceSysCore`).
- **[Г]** Эти библиотеки — из личного окружения разработчиков (masterzorag / LightningMods). Публичного рецепта я не нашёл. Проверка: собрать в Docker-образе и посмотреть, каких `-l` не хватает, затем спросить в Discord/issues Itemzflow или найти форки.
- **Обход (рекомендую)**: прототип функции собрать **отдельно**, как маленький ELF-payload или homebrew на [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) (есть macOS-установщик). Отправлять в BinLoader GoldHEN на порт 9090. В нём нужны только libc, kernel, SceAppInstUtil, SceSysmodule, SceUserService, SQLite (ro). Когда механика заработает, переносить в Itemzflow.

### 3.5 GoldHEN 2.4 на 12.00 через BD-JB
- **[Ф]** Поддержка 12.00/12.02 появилась в GoldHEN **v2.4b18.4**. Сборки распространяются через Ko-fi SiSTR0, а не через GitHub-релизы: последний релиз на GitHub — 2.4b18 от 2024-10-28 ([GitHub](https://github.com/GoldHEN/GoldHEN/releases), [psx-place](https://www.psx-place.com/threads/yt-modded-warefare-goldhen-v2-4b18-4-released-for-ps4-with-12-00-12-02-firmware-support.47954/), [Ko-fi 2.4b18.8](https://ko-fi.com/s/969702e773)).
- **[Ф]** BD-JB на 12.00–12.02 использует kernel-эксплойт **Lapse**, а на 12.50/12.52 — Poops ([ConsoleMods BD-JB](https://consolemods.org/wiki/Bd-jb)). Эксплойт только даёт доступ к ядру, дальше загружается один и тот же payload GoldHEN.
- **[Ф]** Возможности GoldHEN по релиз-ноутам: Homebrew Enabler, sys_dynlib_dlsym Patch, External HDD Support, Rest Mode Support, FTP :2121, BinLoader :9090, Klog :3232.
- **[Ф]** Права ядра Itemzflow получает через `jb.prx` (libjbc, `external/Jailbreak PRX/src/multi-jb.c`): `kexec` = **syscall 11** (`multi-jb.c:137`). Это syscall, который добавляет HEN. Дальше — смещения в `proc/ucred/filedesc` (0x40, 0x48, 0x30, 0xb0), без смещений конкретной прошивки. `main.cpp:448` грузит `jailbreak_me` из `/app0/Media/jb.prx`.
- **[Ф]** Для «Install to USB» нужны: root-права и выход из песочницы (запись в `/user/app`, `/user/appmeta`, `/user/license`, `/system_data/…`), `symlink`, `nmount` для маскировки nullfs (только для варианта b), `dlsym` для системных функций, fake-PKG-патчи ядра для запуска. Всё это уже нужно Itemzflow и GoldHEN, новых патчей ядра нет.
- **[Г]** Отличий BD-JB от PPPwn для нашей задачи быть не должно: после загрузки GoldHEN один и тот же. Отличия касаются только надёжности загрузки и выхода из режима покоя. Проверка: после загрузки с диска отвечают ли FTP :2121, BinLoader :9090, Klog :3232, и проходит ли «Move to USB» в штатном Itemzflow.
- **[Г]** Режим покоя: GoldHEN заявляет Rest Mode Support. Но для игр с USB ps4mount сообщает о зависании при пробуждении, если игра была открыта. Ожидаю то же на PS4: **перед режимом покоя игру закрывать**.

### 3.6 Подготовка диска
- **[Ф]** «Move to USB» запрещён для расширенного хранилища (`EXT_MOVE_NOT_SUPPORTED`, проверка `is_ext_hdd`). ps4mount требует exFAT. Текущий SSD в формате PS4 extended storage **не подходит**.
- Варианты:
  - (А) Перенести игры с SSD на внутренний диск (это медленно) или переустановить их потом. Затем на Mac отформатировать SSD в exFAT с таблицей **MBR**: GUID на PS4 часто не видится. Один раздел, кластер 128 КБ или 1 МБ.
  - (Б) Взять отдельный диск/флешку для экспериментов и не трогать SSD, пока прототип не заработает. **Рекомендую (Б).**
- Пример на Mac (сначала проверь имя диска через `diskutil list`, чтобы не стереть не тот):
  ```
  diskutil eraseDisk ExFAT PS4 MBRFormat /dev/diskN
  ```

---

## 4. План разработки и тестов

**Шаг 0. Бэкапы (обязательно, до любых экспериментов)**
1. Сейвы: Apollo Save Tool → экспорт на USB. Плюс PS Plus Cloud, если есть.
2. Через FTP GoldHEN (порт 2121) скопировать на Mac:
   - `/system_data/priv/mms/app.db`, `appinfo.db`, `addcont.db` (и `*-journal`, если есть)
   - весь `/user/license/`
   - список `/user/app/`, `/user/appmeta/`
3. Хранить в папке с датой. Бэкап app.db делать **при закрытых играх**, сразу после загрузки GoldHEN.

**Шаг 1. Разведка (без изменений системы)**
- Маленький payload на OpenOrbis, отправить в BinLoader :9090. Вывод — в klog :3232 (`nc <ip> 3232`) или в файл на USB.
  - `sceKernelLoadStartModule("libSceAppInstUtil.sprx")` + `sceKernelDlsym` для `sceAppInstUtilAppInstallTitleDir`, `sceAppInstUtilAppInstallAll`. Записать, найдены ли.
  - `statfs("/user")` → `f_fstypename` (ожидаю ufs). `statfs("/mnt/usb0")` → exfatfs.
  - `SELECT * FROM tbl_contentinfo LIMIT 3` в режиме ro/nolock: схема таблиц на 12.00.
- Эксперимент «снимок до/после» из раздела 6.

**Шаг 2. Прототип регистрации (payload, одна маленькая игра)**
- Тестовая игра: маленький fPKG **без патча и DLC**, у которого нет сейвов, которые жалко потерять. Хорошо подходят homebrew-приложения в PKG, например Apollo или любой небольшой порт — их можно потом переустановить.
- Порядок: положить PKG в `/mnt/usb0/itemzflow/apps/<TID>/app.pkg` → сгенерировать файлы (только подтверждённые шагом 1) → `symlink` → `InstallTitleDir` (или `InstallAll` с маскировкой) → дождаться строки в `tbl_contentinfo` → проверить, что плитка появилась → запустить.
- Критерии успеха: плитка с иконкой есть; запуск идёт без CE-ошибок; на внутренний диск ничего не скопировано (`df` до/после); после перезагрузки + GoldHEN игра на месте и запускается.

**Шаг 3. Негативные тесты на той же игре**
1. Запуск при вынутом диске → ожидаю ошибку, но не порчу базы.
2. Режим покоя с закрытой игрой → пробуждение → запуск.
3. Перезагрузка **без** GoldHEN → меню не сломано; игра не запускается — это ожидаемо, fPKG без HEN не работает.
4. Перестановка диска в другой USB-порт (usb0 → usb1) → ссылка сломается; проверить, что ловим это и умеем перевыставить ссылку.
5. Удаление через меню PS4 → PKG на USB **остался**, база в порядке.

**Шаг 4. Перенос в Itemzflow**
- Сначала добиться сборки неизменённого Itemzflow (раздел 3.4). Только потом добавлять `usb_install.cpp` и пункт меню.
- Перед регистрацией автоматически делать бэкап `app.db` в `/data/itemzflow/backup/app.db.<время>`.
- Логирование в `/user/app/ITEM00001/logs/itemzflow_app.log` (уже есть `log_info`).
- Пункт «Relink USB games»: при старте Itemzflow проверять симлинки `/user/app/*/app.pkg`, которые указывают в `/mnt/usb*`, и чинить номер usbN.

**Шаг 5. Расширения** (после стабилизации): патчи (`/user/patch/<TID>/patch.pkg` + `enforce_patch_sfo`), DLC, пакетная регистрация.

---

## 5. Риски и откат

| Риск | Вероятность | Что делать |
|---|---|---|
| ShellCore пометит app.db как повреждённую (особенно при прямой записи `Fix_Game_In_DB`) | средняя | Использовать системные `InstallTitleDir`/`InstallAll`; читать базу только в режиме ro/nolock; бэкап перед каждой операцией |
| `InstallAll` зависает (особенно после покоя) | есть у ps4mount на PS5 | Вызывать в отдельном потоке с тайм-аутом; не регистрировать сразу после выхода из покоя |
| Неверный формат fake-rif/PlayGo для PS4 → игра не запускается | высокая на первом прогоне | Эксперимент из раздела 6; сравнение файлов байт-в-байт |
| Деинсталлятор удалит PKG по симлинку | неизвестно | Перед удалением самим убирать ссылку; тестировать на копии PKG |
| Номер usbN сменился → ссылка битая | высокая | Фиксированный порт; пункт «Relink» |
| Покой с открытой игрой → зависание | высокая | Закрывать игры перед покоем |
| Повреждение exFAT при выдёргивании | средняя | Не вынимать диск при открытой игре; на Mac `fsck_exfat` |

**Откат:**
1. **Одна игра**: в меню PS4 удалить игру → через FTP убрать `/user/app/<TID>` (если остался), `/user/appmeta/<TID>`, `/system_data/priv/appmeta/<TID>`, `fake<CID>.rif/.idx`, только если до теста их не было (сверить с бэкапом).
2. **Меню/база сломаны**:
   - (а) При закрытых играх вернуть бэкап `app.db` через FTP. После этого полная перезагрузка, не покой.
   - (б) Если консоль сама пересоздала базу и fPKG-игры пропали: загрузить GoldHEN → Itemzflow → Settings → **Rebuild DB → Rebuild All** (`rebuild_db()`, `utils.cpp:2255`; восстанавливает строки для fPKG на внутреннем диске), затем **Rebuild DLC**. Симлинкованные на USB игры Rebuild не чинит ([#12](https://github.com/LightningMods/Itemzflow/issues/12)): их зарегистрировать заново нашей функцией.
   - (в) Крайний случай: Safe Mode → **5. Rebuild Database**. Сейвы и игры не удаляет, но сбрасывает порядок и папки в меню. Пользовательские данные при этом не трогаются.
3. **Сейвы**: восстановить через Apollo Save Tool из экспорта.

**Ограничения функции (записать в README форка):**
- Работает только при загруженном GoldHEN и подключённом диске (так и с обычными fPKG).
- После каждой загрузки консоли нужно загрузить GoldHEN с BD-диска до запуска игр.
- Диск — в один и тот же порт.
- Перед режимом покоя закрывать игру.
- Только exFAT, не расширенное хранилище PS4.

---

## 6. Один эксперимент, который даст больше всего информации

**«Снимок до/после штатной установки маленького fPKG»** — на консоли, только чтение плюс одна обычная установка:

1. GoldHEN загружен, игр не запущено. По FTP (:2121) скачать на Mac:
   - рекурсивный листинг с размерами и датами: `/user/app`, `/user/appmeta`, `/user/license`, `/user/playgo` (если есть), `/system_data/playgo` (если есть), `/system_data/priv/appmeta`
   - копии `app.db`, `appinfo.db`
2. Обычной установкой (Itemzflow PKG Installer или GoldHEN Package Installer) поставить **маленький** fPKG.
3. Повторить снимок.
4. На Mac сравнить:
   - `diff` листингов
   - `sqldiff app.db.before app.db.after` (утилита из пакета sqlite, `brew install sqlite`)
   - то же для `appinfo.db`
   - побайтово сравнить `/user/app/<TID>/app.pkg` с исходным PKG (`cmp`)

Это одним заходом ответит на всё, что сейчас под [Г]:
- какие файлы реально создаёт установка на PS4 12.00 (app.json/pbm/xml? PlayGo? `fake*.rif`?);
- какие строки и в какие таблицы пишет система;
- действительно ли `app.pkg` побайтно равен исходному PKG.

После этого генератор из ps4mount можно урезать или поправить под PS4 и сверять наш вывод с эталоном байт-в-байт. Тестовую игру после эксперимента удалить штатно.

---

## 7. Результаты эксперимента «до/после» (2026-10-20, PS4 12.00, GoldHEN)

Тестовый PKG: `IV0000-AZIF00003_00-PAYLOADGUEST0000.pkg` (Payload Guest v0.98, 34 865 152 байт). Установлен штатно из `/data/pkg`.
Данные: `snapshots/before`, `snapshots/after`, `snapshots/app.db.diff`. Скрипт снимка: `tools/ps4snap.py`. Сверка: `proto/gen_test.c` (генераторы ps4mount, собранные на Mac).

### 7.1 Что создаёт установка [Ф]
| Файл | Сверка с генератором ps4mount |
|---|---|
| `/user/app/<TID>/app.pkg` | **побайтно равен исходному PKG** (`cmp`) |
| `/user/app/<TID>/app.json` | отличается: на PS4 **нет** поля `originalFileSize`, остальное совпадает |
| `/user/app/<TID>/app.pbm`, `app.pbm.backup` | отличается: на PS4 поле версии (0x0e) пустое, тип (0x1e) = `0`, а не `0x11`. Так у всех 6 проверенных приложений. Хвост SHA-256 пересчитывается |
| `/user/app/<TID>/app.xml` | отличается только `language_mask`. **[Г]** Это `1 << (63 − язык системы)`: русский = 8 → 2^55 (у 5 из 6 приложений); у Apollo 2^62 = английский. Проверить: `sceSystemServiceParamGetInt(LANG)` |
| `/system_data/playgo/<TID>/pfs.pbm` (+`.backup`) | **совпал байт в байт** |
| `/user/license/free<CID[:19]>.rif` / `.idx` | **совпал байт в байт** с `build_license`; на PS4 префикс имени `free`, а не `fake` |
| `/system_data/priv/appmeta/<TID>/param.sfo` | совпал (запись PKG 0x1000) |
| `/system_data/priv/appmeta/<TID>/ext_info.dat` (44 байт) | **нет в ps4mount** — формат разобрать (есть у всех приложений) |
| `/user/appmeta/<TID>/icon0.png, pic0.png, pic1.png` | совпали (записи PKG) |
| `/user/appmeta/<TID>/pic0.dds, pic1.dds` (1 036 928 байт) | в PKG их нет — **система сама конвертирует PNG→DDS** при установке |
| `/user/playgo/...` (`*.pdb`) | **на PS4 этой папки нет** — часть ps4mount про pdb не нужна |
| `nptitle.dat`, `npbind.dat` | у homebrew отсутствуют; у игр есть (CUSA00557) — переносить из PKG, если есть |

### 7.2 Что меняется в app.db [Ф]
- На PS4 нет `appinfo.db` и нет таблицы `tbl_contentinfo` (её использует `ps4mount/src/db.c`). Проверять регистрацию нужно по `tbl_appinfo` / `tbl_appbrowse_*`.
- Новые строки: ~41 ключ в `tbl_appinfo` + по строке в `tbl_appbrowse_<userId>` **для каждого из 7 пользователей**. Плюс обновлены `tbl_version` (`update_index`, `access_index`, `sync_server`).
- `#_size` = 37 302 272 — больше размера PKG на ~2.4 МБ (по-видимому, плюс appmeta).
- Набор ключей почти совпадает с `Fix_Game_In_DB` из Itemzflow. Есть новые (`SELF_2MIB_PAGE_AMOUNT`) и другие значения (`_view_category`=1).

### 7.3 Выводы для реализации
1. Генераторы ps4mount годятся для PS4 с тремя правками: убрать `originalFileSize`; в `app.pbm` пустая версия и тип 0; `language_mask` из языка системы.
2. Лицензию писать как `/user/license/free<CID[:19]>.rif/.idx` — генератор ps4mount даёт точную копию.
3. Pdb-файлы PlayGo не нужны. `ext_info.dat` и DDS-иконки — открытые вопросы. Предположительно, их создаст сама система при регистрации через `sceAppInstUtilAppInstallTitleDir`/`InstallAll`, проверить на прототипе.
4. Следующий шаг — прототип-payload: проверить через dlsym наличие функций регистрации на 12.00, затем зарегистрировать копию тестового PKG **с USB-флешки**.

## 8. Probe на консоли (2026-10-21) [Ф]

Payload `proto/probe` (обёрнут в .bin через `bootstrap-bin.c` из elfldr) отправлен в GoldHEN BinLoader :9090 скриптом `tools/send_payload.py`. GoldHEN сам запускает .bin в уже «джейлбрейкнутом» процессе `ScePartyDaemon`: uid=0, authid `0x3801000000000013`.
Важно: **пустое подключение к 9090 (`nc -z`) выключает BinLoader** («Error handling payload»). Порт перед отправкой не проверять.

Результаты (klog):
- FW raw `0x12008011` (12.00).
- Запись разрешена: `/system_data/priv/mms/app.db`, `/user/app`, `/user/license`.
- `/user` — **ufs** (`/dev/da0x13.crypt`), значит симлинки в `/user/app` работают так же, как в Itemzflow Move. nullfs, как на PS5, не нужен.
- `/mnt/usb0-7` и `/mnt/ext0` — tmpfs: ни флешка, ни расширенное хранилище в момент теста не были подключены.
- В `libSceAppInstUtil.sprx` на 12.00 есть **все** функции: `sceAppInstUtilInitialize`, `sceAppInstUtilAppInstallTitleDir`, `sceAppInstUtilAppInstallAll`, `AppInstallPkg`, `AppExists`, `AppUnInstall`, `AppGetSize`.
- Лог-файл `/data/ifusb/probe.log` по FTP не нашёлся. Видимо, `/data` в процессе ScePartyDaemon указывает в другое место. Логировать в klog.

### Следующий шаг (не требует флешки)
Проверить регистрацию без копирования на внутреннем диске. PKG лежит в `/user/data/pkg/`, `/user/app/<TID>/app.pkg` — симлинк на него. Плюс сгенерированные файлы из раздела 7, затем `sceAppInstUtilAppInstallTitleDir(tid, "/user/app/", NULL)`. Если игра появится в меню и запустится, механизм доказан; с флешкой меняется только цель ссылки.

## 9. Регистрация без копирования — первый успех (2026-10-21) [Ф]

Payload `proto/reg` (сборка: `proto/reg/build.sh`; сверка с реальной установкой на Mac: «all files match»). Payload Guest предварительно удалён штатно; лог: `snapshots/klog_reg1.txt`; снимок: `snapshots/registered`.

- `place_files()` записал 13 файлов, `/user/app/AZIF00003/app.pkg` → symlink на `/user/data/pkg/IV0000-AZIF00003_00-PAYLOADGUEST0000.pkg`.
- `sceAppInstUtilAppInstallTitleDir("AZIF00003", "/user/app/", NULL)` = `0x0` за 2 с; `sceAppInstUtilAppExists` → flag=1.
- app.db: 41 ключ в `tbl_appinfo` — **идентичны** штатной установке (кроме времени/индексов); строки в `tbl_appbrowse_*` у всех 7 пользователей. Писала их сама система.
- `pic0.dds`/`pic1.dds` система создала сама. `ext_info.dat` и лицензию пишем мы.
- После штатного удаления остаются лицензии `free*.rif/.idx` — это нормально; `/user/app/<TID>` система дочищает в фоне за несколько секунд.

**Плитка в меню появилась, приложение запускается (подтвердил пользователь).**

Осталось проверить: перезагрузка, удаление через меню (не трогает ли PKG по ссылке), затем то же с exFAT-флешки.

### 9.1 Удаление через меню обнуляет PKG по ссылке [Ф] — КРИТИЧНО
Штатное удаление Payload Guest (зарегистрированного через symlink) убрало строки из app.db и папку `/user/app/AZIF00003`, но **исходный PKG в `/user/data/pkg/` стал 0 байт**: система при удалении идёт по ссылке и обрезает целевой файл. С USB это уничтожило бы игру на флешке. (PKG восстановлен с Mac.)
В klog удаление видно заранее: `ContentDeleteScene`, `[BGFT] GameWillDelete(<TID>)` — те же сигналы, на которые реагирует ps4mount.

Варианты защиты:
1. **Ссылка в read-only вид папки (предпочтительно).** Смонтировать папку с PKG через nullfs с `MNT_RDONLY` (например `/mnt/usb0/itemzflow/apps` → `/mnt/ifusb_ro`) и ставить `app.pkg` → `/mnt/ifusb_ro/<TID>/app.pkg`. Запуску нужно только чтение; обрезать файл система не сможет (EROFS). Без монтирования ссылка висит — игра не стартует, но данные целы. Монтирование надо восстанавливать после каждой загрузки (payload/Itemzflow при старте). Проверить: не ломается ли удаление целиком, если обрезать не удалось.
2. Демон, который по klog (`ContentDeleteScene`/`GameWillDelete`) успевает удалить ссылку до деинсталлятора (подход ps4mount). Есть гонка — хуже.
3. Только своё «Удалить» в Itemzflow (сначала unlink ссылки, потом `sceAppInstUtilAppUnInstall`). Не защищает от удаления из меню PS4.

### 9.2 Защита read-only — работает [Ф] (2026-10-21)
- `nmount(nullfs, target=<папка с PKG>, fspath=/mnt/ifusb_ro, MNT_RDONLY)` создаёт вид, через который запись даёт `EROFS`. При этом **`statfs` не показывает `MNT_RDONLY`** (flags=0x10201000), а `MNT_UPDATE`→ro даёт `Operation not supported`. Поэтому payload проверяет реальный отказ записи (`open(O_WRONLY)` → EROFS), а не флаг.
- `app.pkg` → `/mnt/ifusb_ro/<файл>.pkg`. Регистрация: `AppInstallTitleDir = 0` за 1 с. Приложение запускается.
- Удаление через меню: проходит до конца (строк в app.db 0, папки убраны), в klog `sceKernelOpen(/user/app/AZIF00003/app.pkg) failed.ret=0x8002001e` (EROFS) — попытка обрезать файл отклонена. **PKG цел, побайтно равен исходнику.**
- Лог: `snapshots/klog_reg5.txt`.

Следующее: (1) тест с exFAT-флешки; (2) перезагрузка — монтирование `/mnt/ifusb_ro` пропадает, его должен восстанавливать payload/Itemzflow при старте; проверить, что без него игра просто не стартует, а меню не ломается.

## 10. Запуск с exFAT-SSD — работает [Ф] (2026-10-21)
- SSD 1 ТБ (UGREEN CM578) уже был exFAT/MBR, на PS4 монтируется как `/mnt/usb0`. PKG: `/mnt/usb0/itemzflow/apps/AZIF00003/app.pkg`.
- Сборка: `proto/reg/build.sh /mnt/usb0/itemzflow/apps/AZIF00003/app.pkg /mnt/usb0` (второй аргумент — корень, который монтируется read-only целиком).
- `/mnt/usb0` → nullfs read-only `/mnt/ifusb_ro`; запись → EROFS. `app.pkg` → `/mnt/ifusb_ro/itemzflow/apps/AZIF00003/app.pkg`. `AppInstallTitleDir = 0` за 1 с.
- **Payload Guest запускается с SSD** (подтвердил пользователь). Лог: `snapshots/klog_usb1.txt`.

Осталось:
1. Перезагрузка: `/mnt/ifusb_ro` пропадёт → проверить поведение и восстановление монтирования повторным запуском payload (нужен режим «только смонтировать», без регистрации).
2. Настоящая игра (несколько ГБ): скорость загрузки, стабильность, режим покоя.
3. Продукт: payload-«демон» в духе ps4mount (скан `/mnt/usb*/itemzflow/apps`, монтирование, регистрация новых) и/или пункт в форке Itemzflow (сборка Itemzflow пока не решена — нужны нестандартные библиотеки).

## 11. Перезагрузка [Ф] (2026-10-21)
- После полной перезагрузки + GoldHEN: приложение остаётся в app.db (42 ключа) и в меню, appmeta цел; `/mnt/ifusb_ro` исчезает (tmpfs), ссылка `app.pkg` висит. SSD снова `/mnt/usb0`.
- Запуск без монтирования: «Данные повреждены…» на экране; в klog `sceKernelOpen(/user/app/AZIF00003/app.pkg) failed.ret=0x80020002`, `launchApp: 0x80a4000c`. **База не меняется** (contentStatus 0, ключи те же) — ошибка разовая, удалять приложение не нужно.
- `MODE=mount proto/reg/build.sh <pkg> /mnt/usb0` → `ifusb_mount.bin`: только восстанавливает read-only вид и проверяет EROFS. После него приложение **запускается**. Логи: `snapshots/klog_reboot_nomount.txt`, `snapshots/klog_reboot_mount.txt`.

Итог: рабочий цикл после каждой загрузки = BD-JB → GoldHEN → отправить `ifusb_mount.bin` → играть.

### 11.1 Паника ядра: read-only nullfs поверх USB + выдёргивание диска [Ф] — КРИТИЧНО
- SSD вынули из PS4, пока `/mnt/ifusb_ro` (nullfs) лежал прямо на `/mnt/usb0`. AutoMounter принудительно отмонтировал exFAT (`[dounmount] still in use`, `!! suspicious unmount ... fstype='exfatfs'`), nullfs-слой остался «мёртвым».
- После возврата SSD отправка `ifusb_reg` → консоль выключилась сразу после `payload received`, ни одной строки `[ifusb]` (вероятно, statfs/unmount на мёртвом слое → паника). Лог: `snapshots/klog_emsn.txt`.
- ps4mount на PS5 так не делает: nullfs-источник у него на внутреннем диске (`/system_data/ps4mount/...`), на флешку ведут только symlink; «The drive itself is never mounted there». Мёртвые слои он ищет через `getfsstat`, а не `statfs` (`dead_layer`, main.c:164).
- **Вывод: никаких mount-слоёв поверх USB-диска.** Схема 9.2 отменяется для USB.

Новая идея защиты от обнуления (проверить): атрибут exFAT «только чтение» на самом PKG (`chmod 0444` из payload или `chflags uchg` на Mac) + прямой symlink `/user/app/<TID>/app.pkg` → `/mnt/usb0/...`. Выдёргивание диска тогда даёт просто висячую ссылку (проверено в 11: безвредно). Тест: payload делает chmod и `open(O_WRONLY)` без O_TRUNC — ожидаем EACCES/EPERM/EROFS.

## 12. Новая схема без слоёв: прямой symlink + exFAT read-only [Ф] (2026-10-21)
- `chmod(pkg, 0444)` на exFAT ставит атрибут «только чтение» (mode 555), и `open(O_WRONLY)` даже от root → `EACCES` (`proto/attr`, `snapshots/klog_attr.txt`).
- `proto/reg/main.c` v2: никаких mount; `protect_package()` (chmod + проверка отказа записи) до регистрации; `app.pkg` → прямо `/mnt/usb0/...`; режим перепривязки, если тайтл уже наш.
- Payload Guest (перепривязан) запускается. Выдёргивание диска → висячая ссылка (безопасно), после перезагрузки ничего отправлять не нужно.
- Удаление EmuSuNi через меню: `sceKernelOpen(/user/app/EMSN00001/app.pkg) failed.ret=0x8002000d` (EACCES), удаление завершилось, **PKG на SSD цел**.
- EmuSuNi при запуске с SSD: CE-32930-7, `mountApp0Dir: 0x80990019`, PFS mount «… 1 0» (у Payload Guest «… 1 1»). Проверяется штатной установкой + сравнением файлов.

### 12.1 Атрибут «только чтение» ломает запуск части приложений [Ф]
- Генератор верен и для EmuSuNi: 13 файлов штатной установки == наши байт в байт (`snapshots/emsn_real`).
- Штатный EmuSuNi + `app.pkg` → symlink на SSD с атрибутом 0444 (`proto/swap`): висит на лого, CE-38612-0. В klog `BGFT ... Task 00000123 [type=3,subtype=6] : try again (error=0x8002000d)` много раз — BGFT открывает app.pkg на запись при `GameWillStart`.
- Атрибут снят (`proto/chmod`, 0666) → **EmuSuNi запускается с SSD** (штатная регистрация + symlink, как Move to USB в Itemzflow). PKG после запуска побайтно не изменился — запись открывается, но не выполняется.
- Payload Guest с 0444 запускается — зависит от приложения (вероятно, от наличия BGFT-задачи/PlayGo).
- Swap откатан (`ifusb_swap_undo`), штатный EmuSuNi снова на внутреннем диске.

Следующее:
1. Наша регистрация EmuSuNi **без** атрибута — запустится ли (раньше падало на `mountApp0Dir 0x80990019`, тоже возможно из-за EACCES).
2. Защита «только чтение по умолчанию, запись — пока игра запущена»: демон-payload по klog ловит `launchApp(<TID>)` наших тайтлов → chmod 0666, `GameStopped(<TID>)` → chmod 0444. Отказоустойчиво: если демон не запущен, PKG остаётся защищённым (игра может не запуститься, но данные целы). BGFT делает ретраи ~5×0.35 с, это даёт время на реакцию.

### 12.2 Игры (категория gd) требуют задачу BGFT [Ф] (2026-10-21)
- Наша регистрация EmuSuNi без атрибута: `GameWillStart(EMSN00001, 2)` → `[BGFT] ERROR: task not found` → `sceBgftNotifyGameWillStart() ret = 80990019` → CE-32930-7. Payload Guest (приложение, тип 7) задачу не требует.
- Задачи BGFT: `/user/bgft/task/<id hex 8>/d0.pdb` + `d1.pdb` (копия), записи `key u32, len u32, len u32, data`, заголовок 4 нулевых байта, без CRC. Генератор `proto/reg/bgft.c` восстанавливает реальные задачи FPKGi (00000121) и DiPlay (0000011b) **байт в байт** (`proto/reg/bgft_test.c`).
  Поля: 0x95 FW, 0x8b/0x76 время (мкс от 0001-01-01 UTC), 0x64=3/0x6b=6 тип/подтип, 0x74 CID, 0x7c TID, 0x1390 app.json, 0x1388 источник, 0x1389 /user/app/<TID>/app.pkg, 0x69 имя, 0x1a3/0x7a/0x7b размер, 0x1a4 = id−1, 0x191/0x194/0x195 статус чанков PlayGo; с патчем ещё 0x84, 0x13a8/0x13a9/0x13b0, 0x197, 0x19c, 0x1f4=2.
- `place_bgft_task()` пишет задачу с id от 0xf001 (вне диапазона системы, у системы сейчас до ~0x123), не дублирует.
- **BGFT читает задачи только при старте**: без перезагрузки — по-прежнему «task not found»; после перезагрузки (SSD подключён) **EmuSuNi запускается с SSD** (подтвердил пользователь).

Состояние: EmuSuNi зарегистрирован без защиты (атрибут ломает его запуск) — удалять через меню нельзя до защиты «по требованию».

Следующее:
1. Защита по требованию (демон по klog: chmod 0666 при `launchApp`, 0444 при `GameStopped`).
2. Избежать перезагрузки после регистрации игры (найти способ заставить BGFT перечитать задачи).
3. Проверка на большой игре (собственный дамп пользователя).

## 13. Рабочая схема (2026-10-21)
- Регистрация одним запуском: `ps4_register.command` (Mac) → payload `proto/build/ifusb_scan.bin` через BinLoader → сканирует `/mnt/usbN/PKG`, регистрирует новые `.pkg` (символическая ссылка на SSD + задача BGFT + AppInstallTitleDir).
- EmuSuNi зарегистрирован и **запустился без перезагрузки** (подтверждено пользователем).
- Загрузка GoldHEN: диск с BD-JB, Lapse прокнул на 12.00 с первой попытки после полного выключения. SSD подключать до запуска игр.
- Плагин GoldHEN (`ifusb_loader`) отключён: механизм через системный процесс с ошибками, не используется.
- Witcher-PKG (CUSA05571) перенесён из `PKG` в `/mnt/usb0/_moved_out/` по решению пользователя; не регистрировался.
