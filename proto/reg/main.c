/* ifusb_reg: registers one fake PKG without copying it.
 * Writes the install files (place.c) with /user/app/<TID>/app.pkg as a symlink
 * straight to IFUSB_PKG on the USB drive, then asks ShellCore to pick it up
 * with sceAppInstUtilAppInstallAll — this scans /user/app, registers new
 * titles in app.db AND refreshes the home screen (no reboot needed).
 *
 * No mounts on top of the drive: a layer over /mnt/usbN that outlives an
 * unplug panicked the kernel (PLAN_install_to_usb.md 11.1). The package is
 * protected from the uninstaller (which truncates the link target) by the
 * exFAT read-only attribute instead: chmod 0444 makes open(O_WRONLY) fail
 * with EACCES even for root (checked on 12.00).
 *
 * If the title is already registered and its app.pkg is a symlink, only the
 * link is pointed at IFUSB_PKG again (e.g. after the drive path changed). */
#include <dlfcn.h>
#include <errno.h>
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

#ifndef IFUSB_PKG
#error "build with -DIFUSB_PKG=\"/path/to/game.pkg\""
#endif

#define INSTALLALL_TIMEOUT 120 /* seconds; AppInstallAll scans every title */

static FILE *g_log;

static void out(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    klog_printf("[ifusb] %s\n", buf);
    if (g_log) {
        fprintf(g_log, "%s\n", buf);
        fflush(g_log);
    }
}

typedef int (*init_fn)(void);
typedef int (*install_all_fn)(void *);
typedef int (*exists_fn)(const char *title_id, int *flag);
typedef int (*param_int_fn)(int id, int *value);

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

/* Sets the exFAT read-only attribute and checks that a write open is refused.
 * The check uses open(O_WRONLY) without O_TRUNC, so it never changes data. */
__attribute__((unused)) static int protect_package(const char *path) {
    if (chmod(path, 0444))
        out("chmod 0444 %s: %s", path, strerror(errno));
    int fd = open(path, O_WRONLY);
    if (fd >= 0) {
        close(fd);
        out("PROTECTION CHECK FAILED: %s is still writable", path);
        return -1;
    }
    out("protection check: write open of %s -> %s", path, strerror(errno));
    return 0;
}

/* Re-points an existing /user/app/<TID>/app.pkg symlink at path. */
static int relink(const char *tid, const char *path) {
    char link[128], old[256] = "(none)";
    struct stat st;
    snprintf(link, sizeof(link), "/user/app/%s/app.pkg", tid);
    if (lstat(link, &st) == 0) {
        if (!S_ISLNK(st.st_mode)) {
            out("ABORT: %s is a regular file (a normal install), not touched", link);
            return -1;
        }
        ssize_t n = readlink(link, old, sizeof(old) - 1);
        old[n > 0 ? n : 0] = 0;
        if (unlink(link)) {
            out("ABORT: unlink %s: errno %d", link, errno);
            return -1;
        }
    } else {
        out("%s is missing (lstat errno %d), creating it", link, errno);
    }
    if (symlink(path, link)) {
        out("ABORT: relink %s: %s", link, strerror(errno));
        return -1;
    }
    out("relinked %s: %s -> %s", link, old, path);
    return 0;
}

static int system_language(void) {
    int lang = -1;
    void *h = dlopen("/system/common/lib/libSceSystemService.sprx", RTLD_LAZY);
    param_int_fn get = h ? (param_int_fn)dlsym(h, "sceSystemServiceParamGetInt") : NULL;
    if (!get || get(1 /* SCE_SYSTEM_SERVICE_PARAM_ID_LANG */, &lang))
        out("system language unknown, using English");
    return lang;
}

int main(void) {
    const char *pkg_path = IFUSB_PKG;
    char p[256];
    struct stat st;

    mkdir("/data/ifusb", 0777);
    g_log = fopen("/data/ifusb/reg.log", "w");

    out("ifusb_reg v3: %s", pkg_path);
    jailbreak();

    if (stat(pkg_path, &st) || !S_ISREG(st.st_mode)) {
        out("ABORT: package not found: %s", pkg_path);
        return 1;
    }

    void *h = dlopen("/system/common/lib/libSceAppInstUtil.sprx", RTLD_LAZY);
    init_fn init = h ? (init_fn)dlsym(h, "sceAppInstUtilInitialize") : NULL;
    install_all_fn install_all = h ? (install_all_fn)dlsym(h, "sceAppInstUtilAppInstallAll") : NULL;
    exists_fn exists = h ? (exists_fn)dlsym(h, "sceAppInstUtilAppExists") : NULL;
    if (!init || !install_all || !exists) {
        out("ABORT: libSceAppInstUtil functions missing");
        return 1;
    }
    out("sceAppInstUtilInitialize = 0x%08x", init());

    int lang = system_language();
    place_t pl = {.root = "", .pkg_path = pkg_path, .language_mask = place_language_mask(lang), .log = out};
    out("system language %d, language_mask %llu", lang, (unsigned long long)pl.language_mask);

    /* Learn the title ID first, without writing anything. */
    if (place_read_ids(&pl) || !pl.title_id[0]) {
        out("ABORT: cannot read the package or it is not a fake PKG");
        return 1;
    }
    out("title %s, content %s", pl.title_id, pl.content_id);

#ifdef IFUSB_NO_PROTECT
    /* Test build only: the uninstaller can truncate this package. */
    out("WARNING: test build without read-only protection");
#else
    if (protect_package(pkg_path)) {
        out("ABORT: package not protected, nothing registered");
        return 1;
    }
#endif

    int flag = 0;
    int rc = exists(pl.title_id, &flag);
    out("AppExists(%s) = 0x%08x, flag=%d", pl.title_id, rc, flag);
    snprintf(p, sizeof(p), "/user/app/%s", pl.title_id);
    if (flag) {
        /* Ours already: only fix the link. A normal install is never touched. */
        if (relink(pl.title_id, pkg_path))
            return 1;
        uint32_t task = place_bgft_task(&pl, kernel_get_fw_version(), time(NULL));
        out("BGFT task: %08x", task);
        out("done");
        return 0;
    }
    if (lstat(p, &st) == 0) {
        out("ABORT: %s exists but the title is not registered; remove it first", p);
        return 1;
    }

    if (place_files(&pl)) {
        out("ABORT: writing install files failed, removing them");
        place_remove(&pl);
        return 1;
    }
    out("install files written for %s", pl.title_id);
    uint32_t task = place_bgft_task(&pl, kernel_get_fw_version(), time(NULL));
    if (!task) {
        out("ABORT: BGFT task not written, removing install files");
        place_remove(&pl);
        return 1;
    }
    out("BGFT task: %08x", task);

    out("calling AppInstallAll — ShellCore will scan /user/app and refresh UI");
    time_t start = time(NULL);
    rc = install_all(NULL);
    long elapsed = (long)(time(NULL) - start);
    out("AppInstallAll = 0x%08x after %ld s", rc, elapsed);
    if (rc) {
        out("WARNING: AppInstallAll returned error, title may need reboot");
    }

    flag = 0;
    rc = exists(pl.title_id, &flag);
    out("AppExists(%s) after = 0x%08x, flag=%d", pl.title_id, rc, flag);
    snprintf(p, sizeof(p), "/user/app/%s/app.pkg", pl.title_id);
    if (lstat(p, &st) == 0)
        out("%s is %s", p, S_ISLNK(st.st_mode) ? "still a symlink (no copy made)" : "a regular file");
    out("done");
    if (g_log)
        fclose(g_log);
    return 0;
}
