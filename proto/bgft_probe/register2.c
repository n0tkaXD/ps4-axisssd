/* ifusb_bgft_register2: registers the AZIF00003 package with BGFT via API,
 * making the daemon aware of the task in memory (without reboot).
 * Does NOT call StartTask — no download/copy to internal disk.
 * Logs to /data/ifusb/bgft_reg2.log. */
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ps4/kernel.h>
#include <ps4/klog.h>

enum bgft_task_option_t { BGFT_OPT_NONE = 0x0, BGFT_OPT_INVISIBLE = 0x2 };

struct bgft_download_param {
    int user_id;
    int entitlement_type;
    const char *id;
    const char *content_url;
    const char *content_ex_url;
    const char *content_name;
    const char *icon_path;
    const char *sku_id;
    enum bgft_task_option_t option;
    const char *playgo_scenario_id;
    const char *release_date;
    const char *package_type;
    const char *package_sub_type;
    unsigned long package_size;
};

struct bgft_download_param_ex {
    struct bgft_download_param param;
    unsigned int slot;
};

typedef int32_t (*init_fn)(void *);
typedef int32_t (*term_fn)(void);
typedef int32_t (*reg_storage_fn)(struct bgft_download_param_ex *, int32_t *);
typedef int32_t (*reg_fn)(struct bgft_download_param *, int32_t *);
typedef int32_t (*debug_reg_fn)(const char *, int32_t *);
typedef int32_t (*info_fn)(int32_t, void *);
typedef int32_t (*find_fn)(const char *, int32_t *);
typedef int32_t (*start_fn)(int32_t);
typedef int32_t (*progress_fn)(int32_t, void *);

#define HEAP_SIZE (0x400000)

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

int main(void) {
    jailbreak();
    mkdir("/data/ifusb", 0777);
    g_log = fopen("/data/ifusb/bgft_reg2.log", "w");

    out("bgft register2 start, fw=0x%08x", kernel_get_fw_version());

    void *h = dlopen("/system/common/lib/libSceBgft.sprx", RTLD_LAZY);
    if (!h) { out("dlopen failed: %s", dlerror()); goto done; }

    init_fn init = (init_fn)dlsym(h, "sceBgftServiceInit");
    __attribute__((unused)) term_fn term = (term_fn)dlsym(h, "sceBgftServiceTerm");
    reg_storage_fn reg_storage = (reg_storage_fn)dlsym(h, "sceBgftServiceIntDownloadRegisterTaskByStorageEx");
    reg_fn reg_pub = (reg_fn)dlsym(h, "sceBgftServiceDownloadRegisterTask");
    debug_reg_fn debug_reg = (debug_reg_fn)dlsym(h, "sceBgftServiceIntDebugDownloadRegisterPkg");
    info_fn get_info = (info_fn)dlsym(h, "sceBgftServiceIntDownloadGetTaskInfo");
    find_fn find_cid = (find_fn)dlsym(h, "sceBgftServiceDownloadFindTaskByContentId");
    start_fn start_task = (start_fn)dlsym(h, "sceBgftServiceDownloadStartTask");
    progress_fn get_progress = (progress_fn)dlsym(h, "sceBgftServiceDownloadGetProgress");

    out("init=%p reg_storage=%p reg_pub=%p debug_reg=%p",
        (void *)init, (void *)reg_storage, (void *)reg_pub, (void *)debug_reg);
    out("get_info=%p find_cid=%p start=%p progress=%p",
        (void *)get_info, (void *)find_cid, (void *)start_task, (void *)get_progress);

    if (!init || !get_info) {
        out("ABORT: essential functions missing");
        goto done;
    }

    /* Init — 0x80990001 means "already initialized", that's fine */
    static uint8_t heap[HEAP_SIZE];
    struct { void *heap; size_t size; } ip = { heap, HEAP_SIZE };
    int32_t rc = init(&ip);
    out("init rc=0x%08x%s", rc, rc == (int32_t)0x80990001 ? " (already init, ok)" : "");
    if (rc != 0 && rc != (int32_t)0x80990001) {
        out("init failed, aborting");
        goto done;
    }

    /* Check if AZIF00003 task already exists */
    int32_t existing_tid = -1;
    if (find_cid) {
        rc = find_cid("IV0000-AZIF00003_00-PAYLOADGUEST0000", &existing_tid);
        out("find AZIF00003 rc=0x%08x tid=0x%x", rc, existing_tid);
        if (rc == 0 && existing_tid >= 0) {
            out("task already in BGFT memory, no registration needed");
            static uint8_t info_buf[0x1000];
            memset(info_buf, 0, sizeof(info_buf));
            rc = get_info(existing_tid, info_buf);
            out("info rc=0x%08x", rc);
            goto done;
        }
    }

    /* --- Method 1: RegisterTaskByStorageEx --- */
    if (reg_storage) {
        out("trying RegisterTaskByStorageEx...");
        struct bgft_download_param_ex p;
        memset(&p, 0, sizeof(p));
        p.param.entitlement_type = 5;
        p.param.id = "IV0000-AZIF00003_00-PAYLOADGUEST0000";
        p.param.content_url = "/user/data/pkg/IV0000-AZIF00003_00-PAYLOADGUEST0000.pkg";
        p.param.content_name = "PayloadGuest";
        p.param.icon_path = "/user/appmeta/AZIF00003/icon0.png";
        p.param.playgo_scenario_id = "0";
        p.param.option = BGFT_OPT_INVISIBLE;
        p.param.package_size = 34865152;
        p.slot = 0;

        int32_t tid = -1;
        rc = reg_storage(&p, &tid);
        out("reg_storage rc=0x%08x tid=0x%x", rc, tid);
        if (rc == 0 && tid >= 0) {
            out("reg_storage succeeded! checking...");
            static uint8_t info_buf[0x1000];
            memset(info_buf, 0, sizeof(info_buf));
            rc = get_info(tid, info_buf);
            out("info rc=0x%08x", rc);

            /* NOT calling StartTask — that copies PKG to internal disk */
            goto done;
        }
    }

    /* --- Method 3: public RegisterTask --- */
    if (reg_pub) {
        out("trying public RegisterTask...");
        struct bgft_download_param p;
        memset(&p, 0, sizeof(p));
        p.entitlement_type = 5;
        p.id = "IV0000-AZIF00003_00-PAYLOADGUEST0000";
        p.content_url = "/user/app/AZIF00003/app.pkg";
        p.content_name = "PayloadGuest";
        p.icon_path = "/user/appmeta/AZIF00003/icon0.png";
        p.playgo_scenario_id = "0";
        p.option = BGFT_OPT_INVISIBLE;
        p.package_size = 34865152;

        int32_t tid = -1;
        rc = reg_pub(&p, &tid);
        out("reg_pub rc=0x%08x tid=0x%x", rc, tid);
        if (rc == 0 && tid >= 0) {
            out("reg_pub succeeded!");
            static uint8_t info_buf[0x1000];
            memset(info_buf, 0, sizeof(info_buf));
            rc = get_info(tid, info_buf);
            out("info rc=0x%08x", rc);
        }
    }

done:
    out("bgft register2 done");
    if (g_log)
        fclose(g_log);
    return 0;
}
