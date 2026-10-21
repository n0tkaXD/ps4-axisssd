# Исследование: обновление списка приложений PS4 без перезагрузки

Дата: 2026-10-21

---

## 1. Что вызывает необходимость перезагрузки

**Установленный факт.** Текущий payload (`proto/reg/main.c`) использует `sceAppInstUtilAppInstallTitleDir` для регистрации нового тайтла. Эта функция:

- **Работает** на PS4 12.00: возвращает `0x00000000`, после чего `sceAppInstUtilAppExists` подтверждает регистрацию (`flag=1`). Это видно в `snapshots/klog_reg5.txt`:
  ```
  AppInstallTitleDir(AZIF00003) = 0x00000000 after 1 s
  AppExists(AZIF00003) after = 0x00000000, flag=1
  ```
- **Не заставляет ShellCore обновить UI.** Функция добавляет запись в `app.db` (системная БД в `/system_data/priv/mms/app.db`), но **ShellCore (SceShellCore) держит собственный кэш состояния приложений в памяти** и не перечитывает базу после вызова `AppInstallTitleDir`. Процесс SceShellUI (домашний экран) обновляется только при:
  - загрузке системы (ShellCore перечитывает app.db при старте);
  - вызове `sceAppInstUtilAppInstallAll`, который запускает внутренний механизм обхода `/user/app` в самом ShellCore.

**Подтверждение из ps4mount (`ps4mount/src/db.c:1-5`):**
> The system databases are written by the system alone: ShellCore keeps them open with its own cache, and rows written from outside make it flag them as corrupted.

**Подтверждение из ps4mount (`ps4mount/src/main.c:1095-1100`):**
> ShellCore registers every new title under /user/app itself; this also refreshes the home screen and fetches the PSN concept.

Итого: `AppInstallTitleDir` добавляет запись в БД, но не уведомляет ShellCore обновить домашний экран. При перезагрузке ShellCore считывает БД заново — поэтому приложения появляются.

---

## 2. Что делает `sceAppInstUtilAppInstallAll` в исследованных проектах

### 2.1 ps4mount (PS5, etaHEN)

Файл: `ps4mount/src/main.c`, функция `register_call` (строка ~1009), `install_all` (строка ~1199).

`sceAppInstUtilAppInstallAll(NULL)` — основной метод регистрации для FW ≥ 6.00 (`fw.h`, `REG_ALL`). Механизм работы по коду и комментариям ps4mount:

1. ShellCore **сам** обходит все папки `/system_ex/app`, `/system_ex/rnps/apps` и `/user/app`.
2. Для каждой папки, не имеющей записи в app.db, ShellCore создаёт запись и **одновременно обновляет домашний экран**.
3. Обход занимает ~0.7 с на каждую игру (зарегистрированную и нет).
4. **Во время обхода** `/user/app` показывает только добавляемые игры — остальные временно «скрыты». ps4mount использует оптимизацию: монтирует пустую staging-папку поверх `/user/app`, в которой видны только папки новых игр (`mask_user_app`, строка ~1163). Это ускоряет обход и устраняет мерцание.
5. **После возврата** из `AppInstallAll` новые тайтлы уже видны в UI.

### 2.2 ShadowMountPlus (PS4, GoldHEN)

Файл: `proto/refs/ShadowMountPlus/src/sm_shellcore_hooks.c`, `sm_install_queue.c`.

ShadowMountPlus на **FW ≥ 12.00** использует иной подход — «ShellCore TitleDir bridge»:

1. В адресное пространство процесса SceShellCore инжектируется код (bridge blob), который перехватывает внутреннюю точку входа `AppInstallAll` в ShellCore.
2. При вызове `sceAppInstUtilAppInstallAll(NULL)` из ShadowMountPlus, хук в ShellCore перенаправляет вызов на внутреннюю функцию `AppInstallTitleDir` ShellCore'а с конкретным title_id.
3. Это позволяет зарегистрировать один тайтл через `AppInstallAll`, но **внутри ShellCore** вызывается per-title регистрация — быстрее и без обхода всех папок.

Важно: этот подход требует **патчинга памяти процесса SceShellCore** (записи машинного кода), что:
- зависит от конкретных офсетов прошивки;
- сложнее в реализации (требует `ptrace`-подобные операции через kernel exploits);
- не подходит для простого одноразового payload.

Однако ShadowMountPlus также поддерживает **прямой вызов** `sceAppInstUtilAppInstallAll(NULL)` через настройку `app_install_all=1` (`sm_install_queue.c:450`).

---

## 3. Подтверждённые аналоги и альтернативы на PS4 12.00

### 3.1 `sceAppInstUtilAppInstallAll` — **ПОДТВЕРЖДЕНА**

**[Факт]** Функция присутствует в `libSceAppInstUtil.sprx` на PS4 12.00.

Подтверждения:
- PLAN_install_to_usb.md, раздел 8: «В libSceAppInstUtil.sprx на 12.00 есть **все** функции: sceAppInstUtilInitialize, sceAppInstUtilAppInstallTitleDir, sceAppInstUtilAppInstallAll, …»
- `proto/probe/main.c` резолвит эту функцию через `dlsym`. (Результат пробы не найден в логах, но функция также резолвится в ps4mount через тот же механизм для PS5.)
- ps4mount (`fw.h:14-15`) документирует: функция работает начиная с FW 6.00, проверена на 6.50 и 13.20.
- ShadowMountPlus использует её на PS4 12.00 напрямую (`sm_install_queue.c:450`).

**[Не проверено на практике]** Ваш payload ни разу не вызывал `AppInstallAll`. Нужен эксперимент (раздел 6).

### 3.2 `sceAppInstUtilAppInstallTitleDir` — ПОДТВЕРЖДЕНА, но не решает проблему

**[Факт]** Функция работает на PS4 12.00, о чём свидетельствует лог `klog_reg5.txt`. Однако она **не обновляет UI**.

Интересное наблюдение из ShadowMountPlus (`sm_install.c:53-56`):
```c
if (sm_firmware_major_version() >= 12u) {
    g_app_install_title_dir_resolve_attempted = true;
    return NULL;  // На FW >= 12.00 — не использовать AppInstallTitleDir напрямую
}
```
ShadowMountPlus **явно отказывается** от прямого вызова `AppInstallTitleDir` на FW ≥ 12.00 и вместо неё использует ShellCore bridge, который вызывает **внутреннюю** реализацию `AppInstallTitleDir` **внутри ShellCore**. Разница: внешний вызов через `libSceAppInstUtil.sprx` пишет в БД, но не обновляет ShellCore; внутренний вызов через bridge обновляет и БД, и UI.

### 3.3 ShellCore bridge (как в ShadowMountPlus) — теоретически возможен, практически сложен

Требует:
- Поиск PID SceShellCore
- Определение image base
- Разрешение офсетов для конкретной прошивки (12.00)
- Запись машинного кода в адресное пространство другого процесса
- Обработка перехвата и восстановление оригинальных инструкций

Это полноценный хук-фреймворк. Для одноразового payload — несоразмерная сложность.

### 3.4 Прямая запись в app.db — **ОПАСНА**

**[Факт из ps4mount]** ShellCore мониторит целостность app.db. Запись из внешнего процесса приводит к пометке базы как повреждённой и её пересозданию при следующей загрузке (`ps4mount/src/main.c:1232-1235`).

Itemzflow использует прямую запись (`Fix_Game_In_DB`, `sql.cpp:411`), но это вспомогательный механизм для «Rebuild DB», а не для runtime-регистрации. Риск повреждения высок.

---

## 4. Рекомендуемый подход

### Рекомендация: заменить `AppInstallTitleDir` на `AppInstallAll`

**Обоснование:**
1. `AppInstallAll` — штатный механизм регистрации для FW ≥ 6.00 (ваш FW 12.00 попадает в этот диапазон).
2. Эта функция заставляет ShellCore **самостоятельно** обойти `/user/app`, обнаружить новый тайтл, добавить его в app.db и **обновить домашний экран** — всё в одном вызове.
3. Не требует патчинга ShellCore, записи в app.db или других опасных операций.
4. ps4mount успешно использует этот подход на PS5 (FW 6.50, 13.20), ShadowMountPlus — на PS4.

**Оптимизация (необязательная, но желательная):**
Маскировка `/user/app` по аналогии с ps4mount — монтирование staging-папки поверх `/user/app`, содержащей только папку нового тайтла. Это:
- ускоряет обход (вместо всех игр — только одна);
- устраняет мерцание UI (остальные игры не исчезают на время обхода).

### Альтернативы (в порядке приоритета)

| # | Подход | Сложность | Риск | Статус |
|---|--------|-----------|------|--------|
| 1 | `AppInstallAll` без маскировки | Минимальная | Низкий (мерцание UI на время обхода) | **Рекомендуется для первого теста** |
| 2 | `AppInstallAll` с маскировкой | Средняя | Низкий | Рекомендуется для финальной версии |
| 3 | ShellCore bridge | Высокая | Средний (офсеты FW-зависимые) | Только если 1-2 не работают |
| 4 | Перезагрузка | Нулевая | Нулевой | Текущее поведение, fallback |

---

## 5. Конкретные изменения исходников

### 5.1 Минимальное изменение: `AppInstallAll` без маскировки

Файл: `proto/reg/main.c`

Текущий код (строки ~130-155) использует `sceAppInstUtilAppInstallTitleDir`. Нужно:

1. Добавить резолвинг `sceAppInstUtilAppInstallAll`.
2. Вызвать `AppInstallAll(NULL)` вместо `AppInstallTitleDir`.
3. Дождаться появления тайтла в БД (poll `sceAppInstUtilAppExists`).

```c
/* Добавить к существующим typedef'ам: */
typedef int (*install_all_fn)(void *);

/* В main(), после резолвинга существующих функций: */
install_all_fn install_all = h ? (install_all_fn)dlsym(h, "sceAppInstUtilAppInstallAll") : NULL;
if (!install_all) {
    out("WARNING: sceAppInstUtilAppInstallAll not found, falling back to TitleDir");
    /* fallback к текущему поведению */
}

/* Вместо title_dir_thread / g_title_dir: */
if (install_all) {
    out("calling AppInstallAll");
    int rc = install_all(NULL);
    out("AppInstallAll = 0x%08x", rc);
} else {
    /* текущий код с AppInstallTitleDir */
}

/* После вызова — poll до появления в БД: */
int wait = 0;
flag = 0;
while (!flag && wait < 30) {
    usleep(1000 * 1000);
    exists(pl.title_id, &flag);
    wait++;
}
out("AppExists(%s) after %d s = flag=%d", pl.title_id, wait, flag);
```

### 5.2 С маскировкой (оптимизация)

Если без маскировки обход занимает слишком много времени или мерцает UI:

```c
#include <sys/mount.h>

/* Создать staging-папку только с новым тайтлом */
#define STAGE_DIR "/system_data/ifusb_stage"

static int mask_user_app(const char *tid) {
    char p[128];
    mkdir(STAGE_DIR, 0777);
    snprintf(p, sizeof(p), STAGE_DIR "/%s", tid);
    mkdir(p, 0777);

    /* nullfs mount staging поверх /user/app */
    struct iovec iov[] = {
        {(void *)"fstype", 7}, {(void *)"nullfs", 7},
        {(void *)"from", 5},   {(void *)STAGE_DIR, strlen(STAGE_DIR) + 1},
        {(void *)"fspath", 7}, {(void *)"/user/app", 10},
    };
    if (nmount(iov, 6, 0)) {
        out("mask_user_app nmount: %s", strerror(errno));
        return -1;
    }
    /* mount папки тайтла из hold поверх staging */
    char hold[128], target[128];
    snprintf(hold, sizeof(hold), "/user/app/%s", tid);  /* через staging это будет STAGE_DIR/TID */
    /* Нет: нужен отдельный hold_dir для файлов тайтла, как в ps4mount.
       Для первого теста проще без маскировки. */
    return 0;
}
```

**Важно:** маскировка требует, чтобы файлы тайтла лежали в отдельной папке (не в самом `/user/app/<TID>`), которая монтируется поверх staging. Это усложняет архитектуру. Рекомендую **сначала проверить без маскировки**.

---

## 6. План тестирования

### Предпосылки
- Бэкап app.db и addcont.db уже сделан (`backups/db_20261021_045631/`).
- Тестовый PKG (AZIF00003) с категорией `gde` доступен.

### Шаг 1: Убедиться, что `sceAppInstUtilAppInstallAll` резолвится

Модифицировать `proto/probe/main.c` — он уже резолвит эту функцию через `dlsym`. Отправить на консоль, проверить лог:
```
dlsym sceAppInstUtilAppInstallAll: FOUND (0x...)
```

Если `FOUND` — переходить к шагу 2. Если `missing` — функция отсутствует в этой прошивке (маловероятно по имеющимся данным), нужен альтернативный подход.

### Шаг 2: Тест `AppInstallAll` на чистом тайтле

1. Удалить тайтл AZIF00003, если он зарегистрирован (`sceAppInstUtilAppUnInstall` или через консоль).
2. Выполнить payload с `place_files` + `place_bgft_task` (как сейчас).
3. Вместо `AppInstallTitleDir` вызвать `AppInstallAll(NULL)`.
4. В klog проверить:
   - Возвращённый код (`0x00000000` = успех).
   - Время выполнения (ожидание: 5–60 с в зависимости от числа установленных игр).
   - `AppExists` → `flag=1`.
5. **Не перезагружая**, проверить домашний экран: появилась ли плитка AZIF00003.

### Шаг 3: Проверить запуск

Если плитка появилась — запустить приложение. В klog должно быть:
```
[SceLncService] launchApp(AZIF00003)
[BGFT] GameStartBoot(AZIF00003, ...) start
[BGFT] GameWillStart(AZIF00003, ...) start
```

### Шаг 4: Проверить стабильность

- Перезагрузка: приложение должно остаться.
- Режим покоя → пробуждение: приложение должно быть на месте.
- Извлечение USB → вставка: приложение должно запуститься.

### Безопасный откат

Если `AppInstallAll` вызывает проблемы (зависание, ошибки):
1. Payload уже пишет лог в klog — причина будет видна.
2. Файлы тайтла можно удалить через `place_remove`.
3. Бэкап app.db позволяет восстановить состояние через FTP.
4. В крайнем случае — «Rebuild Database» из Safe Mode.

---

## Итого

| Вопрос | Ответ |
|--------|-------|
| Причина перезагрузки | `AppInstallTitleDir` пишет в БД, но не уведомляет ShellCore обновить UI. ShellCore перечитывает БД только при загрузке. |
| Что делает `AppInstallAll` в ps4mount | Заставляет ShellCore самому обойти `/user/app`, зарегистрировать новые тайтлы и обновить домашний экран. |
| Есть ли аналог на PS4 12.00 | Да: `sceAppInstUtilAppInstallAll` присутствует и используется ShadowMountPlus. |
| Рекомендация | Заменить `AppInstallTitleDir` на `AppInstallAll(NULL)` в payload. |
| Изменения кода | Минимальные: добавить `dlsym` для `AppInstallAll`, вызвать вместо `AppInstallTitleDir`, poll `AppExists`. |
| Тестирование | Шаг 1 → probe, Шаг 2 → регистрация без ребута, Шаг 3 → запуск, Шаг 4 → стабильность. |
