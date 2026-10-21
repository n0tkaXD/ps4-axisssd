/* ifusb_inject: runs inside a system process (loaded by the loader stub).
 * Registers every new .pkg of the PKG folder on USB drives. */
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "place.h"
#include "bgft.h"
#include "plugin_common.h"

attr_public const char *g_pluginName = "ifusb_inject";
attr_public const char *g_pluginDesc = "Registers PS4 packages from the PKG folder of USB drives";
attr_public const char *g_pluginAuth = "ifusb";
attr_public uint32_t g_pluginVersion = 0x00000100;

#define PKG_DIR "/mnt/usb0/PKG"
#define FW_RAW 0x12008011u

extern int sceSysmoduleLoadModule(uint16_t id);
extern int sceAppInstUtilInitialize(void);
extern int sceAppInstUtilAppInstallTitleDir(const char *title_id, const char *dir, void *reserved);
#define SYSMODULE_APPINSTUTIL 0x80000014

static void fileLog(const char *fmt, ...) {
    char m[200] = {0};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m, sizeof(m), fmt, ap);
    va_end(ap);
    extern int g_logfd_inj;
    if (g_logfd_inj >= 0) {
        write(g_logfd_inj, m, strlen(m));
        write(g_logfd_inj, "\n", 1);
    }
}
#define nolog fileLog

static int is_pkg(const char *name) {
    size_t n = strlen(name);
    return n > 4 && !strcmp(name + n - 4, ".pkg") && name[0] != '.';
}

static int is_installed(const char *tid) {
    char p[64];
    struct stat st;
    snprintf(p, sizeof(p), "/user/app/%s", tid);
    return lstat(p, &st) == 0;
}

static int register_one(const char *path) {
    place_t p = {.root = "", .pkg_path = path, .language_mask = place_language_mask(8), .log = nolog};
    if (place_read_ids(&p) || !p.title_id[0])
        return 0;
    if (is_installed(p.title_id))
        return 0;
    if (place_files(&p))
        return 0;
    if (!place_bgft_task(&p, FW_RAW, (int64_t)time(NULL)))
        return 0;
    int rc = sceAppInstUtilAppInstallTitleDir(p.title_id, "/user/app/", NULL);
    return rc == 0 && is_installed(p.title_id);
}

/* Diagnostic run: every step is appended to /data/ifusb/reg.log on the console.
 * Notifications from the thread are not shown reliably, the file is. */
static int g_fd = -1;
int g_logfd_inj = -1;

static void logf_(const char *fmt, int v) {
    char m[200] = {0};
    snprintf(m, sizeof(m), fmt, v);
    if (g_fd >= 0)
        write(g_fd, m, strlen(m));
    if (g_fd >= 0)
        write(g_fd, "\n", 1);
}

static void *register_thread(void *arg) {
    (void)arg;
    mkdir("/data/ifusb", 0777);
    g_fd = open("/data/ifusb/reg.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    g_logfd_inj = g_fd;
    logf_("s1 sysmodule %d", sceSysmoduleLoadModule(SYSMODULE_APPINSTUTIL));
    logf_("s2 init %d", sceAppInstUtilInitialize());
    DIR *d = opendir(PKG_DIR);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (!is_pkg(e->d_name))
            continue;
        char path[256];
        snprintf(path, sizeof(path), PKG_DIR "/%s", e->d_name);
        place_t p = {.root = "", .pkg_path = path, .language_mask = place_language_mask(8), .log = nolog};
        logf_("s3 ids %d", place_read_ids(&p));
        logf_("s4 before files", 0);
        logf_("s4 files %d", place_files(&p));
        logf_("s5 task %d", (int)place_bgft_task(&p, FW_RAW, (int64_t)time(NULL)));
        char link[128];
        snprintf(link, sizeof(link), "/user/app/%s/app.pkg", p.title_id);
        logf_("s6 link %d", symlink(path, link));
        struct stat st;
        logf_("s7 lstat %d", lstat(link, &st));
        logf_("s8 titledir %d", sceAppInstUtilAppInstallTitleDir(p.title_id, "/user/app/", NULL));
        logf_("s9 done %d", 0);
    }
    if (d)
        closedir(d);
    if (g_fd >= 0)
        close(g_fd);
    return NULL;
}

s32 attr_module_hidden module_start(s64 argc, const void *args) {
    (void)argc; (void)args;
    pthread_t th;
    if (pthread_create(&th, NULL, register_thread, NULL) == 0)
        pthread_detach(th);
    return 0;
}

s32 attr_module_hidden module_stop(s64 argc, const void *args) { (void)argc; (void)args; return 0; }

int32_t attr_public plugin_load(int32_t argc, const char *argv[]) { (void)argc; (void)argv; return 0; }
int32_t attr_public plugin_unload(int32_t argc, const char *argv[]) { (void)argc; (void)argv; return 0; }
