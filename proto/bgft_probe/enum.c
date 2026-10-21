/* ifusb_bgft_enum: resolves every known sceBgft* symbol from libSceBgft.sprx
 * and logs which ones exist on PS4 12.00.  Read-only, changes nothing. */
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
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
    if (g_log) {
        fprintf(g_log, "%s\n", buf);
        fflush(g_log);
    }
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

static const char *k_syms[] = {
    /* lifecycle */
    "sceBgftServiceInit",
    "sceBgftServiceTerm",
    /* register variants */
    "sceBgftServiceIntDownloadRegisterTask",
    "sceBgftServiceIntDownloadRegisterTaskByStorageEx",
    "sceBgftServiceIntDebugDownloadRegisterPkg",
    "sceBgftServiceRawDownloadRegisterTask",
    "sceBgftServiceDownloadRegisterTask",
    "sceBgftServiceDownloadRegisterTaskStore",
    /* start / stop / pause / resume */
    "sceBgftServiceDownloadStartTask",
    "sceBgftServiceDownloadStartTaskAll",
    "sceBgftServiceDownloadPauseTask",
    "sceBgftServiceDownloadResumeTask",
    "sceBgftServiceDownloadStopTask",
    "sceBgftServiceDownloadDeleteTask",
    /* info / query */
    "sceBgftServiceDownloadGetTaskInfo",
    "sceBgftServiceIntDownloadGetTaskInfo",
    "sceBgftServiceDownloadFindTaskByContentId",
    "sceBgftServiceDownloadFindActivePatchTask",
    "sceBgftServiceDownloadFindActiveGameTask",
    "sceBgftServiceDownloadGetProgress",
    "sceBgftServiceDownloadGetTaskList",
    "sceBgftServiceDownloadGetTaskListAll",
    /* notify */
    "sceBgftNotifyGameWillStart",
    "sceBgftNotifyGameStartBoot",
    "sceBgftNotifyGameStopped",
    "sceBgftNotifyPatchReady",
    /* content / storage */
    "sceBgftServiceDownloadGetContentId",
    "sceBgftServiceDownloadGetStorageStatus",
    "sceBgftServiceDownloadSetStorageStatus",
    /* misc */
    "sceBgftServiceDownloadSetVisible",
    "sceBgftServiceDownloadComplete",
    "sceBgftServiceDownloadCompleteTask",
    "sceBgftServiceDownloadTaskCommit",
    "sceBgftServiceIntDownloadSetTaskComplete",
    "sceBgftServiceIntDownloadReloadTask",
    "sceBgftServiceIntReloadTasks",
    "sceBgftServiceReloadTasks",
    "sceBgftServiceRefreshTasks",
    "sceBgftServiceRescanTasks",
    NULL,
};

int main(void) {
    jailbreak();
    mkdir("/data/ifusb", 0777);
    g_log = fopen("/data/ifusb/bgft_enum.log", "w");

    out("bgft enum start, fw=0x%08x", kernel_get_fw_version());

    void *h = dlopen("/system/common/lib/libSceBgft.sprx", RTLD_LAZY);
    if (!h) {
        out("dlopen libSceBgft: %s", dlerror());
        goto done;
    }
    out("libSceBgft loaded");

    int found = 0, total = 0;
    for (int i = 0; k_syms[i]; i++) {
        total++;
        void *f = dlsym(h, k_syms[i]);
        out("  %s: %s (%p)", k_syms[i], f ? "FOUND" : "missing", f);
        if (f) found++;
    }
    out("total: %d/%d found", found, total);

done:
    out("bgft enum done");
    if (g_log)
        fclose(g_log);
    return 0;
}
