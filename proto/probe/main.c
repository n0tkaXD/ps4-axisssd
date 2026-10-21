/* ifusb probe v2: checks API availability and calls AppInstallAll once to
 * verify that ShellCore processes the request. No new titles are staged,
 * so the call is a no-op scan of /user/app (existing titles are untouched).
 * Results go to klog and /data/ifusb/probe.log. */
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <ps4/kernel.h>
#include <ps4/klog.h>

static FILE *g_log;

static void out(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    klog_printf("[ifusb] %s\n", buf);
    printf("%s\n", buf);
    if (g_log) {
        fprintf(g_log, "%s\n", buf);
        fflush(g_log);
    }
}

static void fs_info(const char *path) {
    struct statfs sf;
    if (statfs(path, &sf))
        out("statfs %s: %s", path, strerror(errno));
    else
        out("statfs %s: type=%s from=%s on=%s", path, sf.f_fstypename, sf.f_mntfromname, sf.f_mntonname);
}

static void access_info(const char *path) {
    out("access %s: R=%d W=%d", path, access(path, R_OK) == 0, access(path, W_OK) == 0);
}

static void creds(const char *when) {
    pid_t pid = getpid();
    out("[%s] pid=%d uid=%d authid=0x%016llx prison=%p rootdir=%p", when, pid, getuid(),
        (unsigned long long)kernel_get_ucred_authid(pid), (void *)kernel_get_ucred_prison(pid),
        (void *)kernel_get_proc_rootdir(pid));
}

static const char *k_syms[] = {
    "sceAppInstUtilInitialize",
    "sceAppInstUtilAppInstallTitleDir",
    "sceAppInstUtilAppInstallAll",
    "sceAppInstUtilAppInstallPkg",
    "sceAppInstUtilAppExists",
    "sceAppInstUtilAppUnInstall",
    "sceAppInstUtilAppGetSize",
    NULL,
};

int main(void) {
    mkdir("/data/ifusb", 0777);
    g_log = fopen("/data/ifusb/probe.log", "w");

    uint32_t fw = kernel_get_fw_version();
    out("ifusb probe v2, fw raw=0x%08x", fw);
    creds("start");

    /* Root + escape the sandbox, the way homebrew loaders do it. */
    pid_t pid = getpid();
    kernel_set_ucred_uid(pid, 0);
    kernel_set_ucred_ruid(pid, 0);
    kernel_set_ucred_svuid(pid, 0);
    kernel_set_ucred_rgid(pid, 0);
    kernel_set_ucred_svgid(pid, 0);
    kernel_set_ucred_prison(pid, kernel_get_ucred_prison(1));
    kernel_set_proc_rootdir(pid, kernel_get_proc_rootdir(1));
    kernel_set_proc_jaildir(pid, 0);
    /* Same values as Itemzflow's jbc_jailbreak_cred (multi-jb.c:351-353). */
    kernel_set_ucred_authid(pid, 0x3801000000000013l);
    static const uint8_t all_caps[16] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    kernel_set_ucred_caps(pid, all_caps);
    creds("after jailbreak");

    access_info("/system_data/priv/mms/app.db");
    access_info("/user/app");
    access_info("/user/license");
    fs_info("/user");
    fs_info("/user/app");
    fs_info("/system_data");
    for (int i = 0; i < 8; i++) {
        char p[32];
        snprintf(p, sizeof(p), "/mnt/usb%d", i);
        if (access(p, F_OK) == 0)
            fs_info(p);
    }
    fs_info("/mnt/ext0");

    void *h = dlopen("/system/common/lib/libSceAppInstUtil.sprx", RTLD_LAZY);
    if (!h) {
        out("dlopen libSceAppInstUtil: %s", dlerror());
        goto done;
    }
    for (int i = 0; k_syms[i]; i++) {
        void *f = dlsym(h, k_syms[i]);
        out("dlsym %s: %s (%p)", k_syms[i], f ? "FOUND" : "missing", f);
    }

    /* --- actually call AppInstallAll to verify ShellCore processes it --- */
    typedef int (*init_fn)(void);
    typedef int (*install_all_fn)(void *);
    init_fn ai_init = (init_fn)dlsym(h, "sceAppInstUtilInitialize");
    install_all_fn ai_all = (install_all_fn)dlsym(h, "sceAppInstUtilAppInstallAll");

    if (!ai_init || !ai_all) {
        out("AppInstallAll test SKIPPED: init=%p all=%p", (void *)ai_init, (void *)ai_all);
        goto done;
    }

    int rc = ai_init();
    out("sceAppInstUtilInitialize = 0x%08x", rc);

    out("calling sceAppInstUtilAppInstallAll(NULL) — no new titles staged, expect a no-op scan");
    time_t t0 = time(NULL);
    rc = ai_all(NULL);
    time_t elapsed = time(NULL) - t0;
    out("sceAppInstUtilAppInstallAll = 0x%08x, elapsed %ld s", rc, (long)elapsed);

done:
    out("probe done");
    if (g_log)
        fclose(g_log);
    return 0;
}
