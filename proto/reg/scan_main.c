/* ifusb_scan: payload for GoldHEN BinLoader. Registers every new .pkg found in
 * the PKG folder of each USB drive, without copying it. Logs to klog ([ifusb]). */
#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <ps4/kernel.h>
#include <ps4/klog.h>

#include "place.h"

#define FW_RAW 0x12008011u

/* System notification (same request layout as the PS5 tools use). */
typedef struct { char useless[45]; char message[3075]; } notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static void notify(const char *text) {
    notify_request_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.message, sizeof(req.message), "%s", text);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}
#define TITLEDIR_TIMEOUT 90

static void out(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    klog_printf("[ifusb] %s\n", buf);
}

static void quiet(const char *fmt, ...) { (void)fmt; }

typedef int (*init_fn)(void);
typedef int (*title_dir_fn)(const char *title_id, const char *dir, void *reserved);
typedef int (*exists_fn)(const char *title_id, int *flag);

static title_dir_fn g_title_dir;
static const char *g_tid;
static volatile int g_done, g_rc;

static void *title_dir_thread(void *arg) {
    (void)arg;
    g_rc = g_title_dir(g_tid, "/user/app/", NULL);
    g_done = 1;
    return NULL;
}

static void jailbreak(void) {
    pid_t pid = getpid();
    static const uint8_t caps[16] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    kernel_set_ucred_uid(pid, 0);
    kernel_set_ucred_ruid(pid, 0);
    kernel_set_ucred_svuid(pid, 0);
    kernel_set_ucred_rgid(pid, 0);
    kernel_set_ucred_svgid(pid, 0);
    kernel_set_ucred_prison(pid, kernel_get_ucred_prison(1));
    kernel_set_proc_rootdir(pid, kernel_get_proc_rootdir(1));
    kernel_set_proc_jaildir(pid, 0);
    kernel_set_ucred_authid(pid, 0x3801000000000013l);
    kernel_set_ucred_caps(pid, caps);
}

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

static int register_one(const char *path, title_dir_fn title_dir, exists_fn exists) {
    place_t p = {.root = "", .pkg_path = path, .language_mask = place_language_mask(8), .log = quiet};
    if (place_read_ids(&p) || !p.title_id[0]) {
        out("skip (not a fake pkg): %s", path);
        return 0;
    }
    /* An empty /user/app/<ID> left after a delete from the menu is not a
     * registration: remove it (rmdir fails on non-empty folders, so real
     * installs are never touched). */
    char leftover[64];
    snprintf(leftover, sizeof(leftover), "/user/app/%s", p.title_id);
    if (rmdir(leftover) == 0)
        out("removed empty leftover folder: %s", leftover);
    if (is_installed(p.title_id)) {
        out("already registered: %s", p.title_id);
        return 0;
    }
    if (place_files(&p)) {
        out("files failed: %s", p.title_id);
        place_remove(&p);
        return 0;
    }
    if (place_bgft_task(&p, FW_RAW, (int64_t)time(NULL)) == 0) {
        out("bgft task failed: %s", p.title_id);
        place_remove(&p);
        return 0;
    }
    g_tid = p.title_id;
    pthread_t th;
    if (pthread_create(&th, NULL, title_dir_thread, NULL) == 0)
        pthread_detach(th);
    else
        title_dir_thread(NULL);
    time_t start = time(NULL);
    while (!g_done && time(NULL) - start < TITLEDIR_TIMEOUT)
        usleep(250 * 1000);
    if (!g_done) {
        out("registration did not return: %s", p.title_id);
        return 0;
    }
    int ok = (g_rc == 0) && is_installed(p.title_id);
    int flag = 0;
    exists(p.title_id, &flag);
    out("%s: %s (rc=0x%08x, exists=%d)", p.title_id, ok ? "REGISTERED" : "not registered", g_rc, flag);
    return ok;
}

int main(void) {
    out("ifusb_scan v1 start");
    jailbreak();
    void *h = dlopen("/system/common/lib/libSceAppInstUtil.sprx", RTLD_LAZY);
    init_fn init = h ? (init_fn)dlsym(h, "sceAppInstUtilInitialize") : NULL;
    g_title_dir = h ? (title_dir_fn)dlsym(h, "sceAppInstUtilAppInstallTitleDir") : NULL;
    exists_fn exists = h ? (exists_fn)dlsym(h, "sceAppInstUtilAppExists") : NULL;
    if (!init || !g_title_dir || !exists) {
        out("ABORT: AppInstUtil functions missing");
        return 1;
    }
    out("AppInstUtil init = 0x%08x", init());
    notify("AxisSSD created by n0tkaXD");
    int found = 0, added = 0;
    for (int i = 0; i < 8; i++) {
        char dir[64];
        snprintf(dir, sizeof(dir), "/mnt/usb%d/PKG", i);
        DIR *d = opendir(dir);
        if (!d)
            continue;
        out("scanning %s", dir);
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!is_pkg(e->d_name))
                continue;
            found++;
            char path[256];
            snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
            if (register_one(path, g_title_dir, exists))
                added++;
        }
        closedir(d);
    }
    out("done: %d pkg(s) found, %d registered. Restart the console to see them", found, added);
    return 0;
}
