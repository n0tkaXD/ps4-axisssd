/* ifusb_bgft_register: registers the NP2kai package with BGFT through its API,
 * WITHOUT starting it. Then reports whether BGFT knows the new task. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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
struct bgft_download_param_ex { struct bgft_download_param param; unsigned int slot; };

typedef int32_t (*init_fn)(void *params);
typedef int32_t (*reg_fn)(struct bgft_download_param_ex *params, int32_t *taskId);
typedef int32_t (*info_fn)(int32_t taskId, void *info);

#define PKG "/mnt/usb0/PKG/IV0000-BREW00984_00-NP2KAIBOOTSTRAP0.pkg"
#define HEAP_SIZE (0x200000)

int main(void) {
    klog_printf("[ifusb] bgft register start\n");
    void *h = dlopen("/system/common/lib/libSceBgft.sprx", RTLD_LAZY);
    if (!h) { klog_printf("[ifusb] dlopen failed\n"); return 1; }
    init_fn init = (init_fn)dlsym(h, "sceBgftServiceInit");
    reg_fn reg = (reg_fn)dlsym(h, "sceBgftServiceIntDownloadRegisterTaskByStorageEx");
    info_fn info = (info_fn)dlsym(h, "sceBgftServiceIntDownloadGetTaskInfo");
    if (!init || !reg || !info) { klog_printf("[ifusb] symbols missing\n"); return 1; }

    static uint8_t heap[HEAP_SIZE];
    struct { void *heap; size_t size; } ip = { heap, HEAP_SIZE };
    klog_printf("[ifusb] init rc=0x%08x\n", init(&ip));

    struct bgft_download_param_ex p;
    memset(&p, 0, sizeof(p));
    p.param.entitlement_type = 5;
    p.param.id = "";
    p.param.content_url = PKG;
    p.param.content_name = "NP2kai";
    p.param.icon_path = "/update/fakepic.png";
    p.param.playgo_scenario_id = "0";
    p.param.option = BGFT_OPT_INVISIBLE;
    p.slot = 0;

    int32_t tid = -1;
    int rc = reg(&p, &tid);
    klog_printf("[ifusb] register rc=0x%08x task=0x%x\n", rc, tid);
    if (rc == 0) {
        static uint8_t buf[0x1000];
        memset(buf, 0, sizeof(buf));
        klog_printf("[ifusb] info rc=0x%08x\n", info(tid, buf));
    }
    klog_printf("[ifusb] bgft register done (NOT started)\n");
    return 0;
}
