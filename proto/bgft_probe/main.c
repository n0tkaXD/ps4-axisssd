/* ifusb_bgft_probe: READ-ONLY. Asks BGFT for task info of two known task ids
 * and logs the return codes to klog. Changes nothing. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ps4/klog.h>

typedef int32_t (*get_info_fn)(int32_t taskId, void *info);
typedef int32_t (*init_fn)(void *params);
struct init_params { void *heap; size_t heapSize; };
#define HEAP_SIZE (0x200000)

int main(void) {
    klog_printf("[ifusb] bgft probe start\n");
    void *h = dlopen("/system/common/lib/libSceBgft.sprx", RTLD_LAZY);
    if (!h) {
        klog_printf("[ifusb] dlopen libSceBgft failed\n");
        return 1;
    }
    get_info_fn get = (get_info_fn)dlsym(h, "sceBgftServiceIntDownloadGetTaskInfo");
    if (!get) {
        klog_printf("[ifusb] GetTaskInfo not found\n");
        return 1;
    }
    init_fn init = (init_fn)dlsym(h, "sceBgftServiceInit");
    struct init_params ip = {0};
    static uint8_t heap_mem[HEAP_SIZE];
    ip.heap = heap_mem;
    ip.heapSize = HEAP_SIZE;
    int irc = init ? init(&ip) : -1;
    klog_printf("[ifusb] bgft init rc=0x%08x\n", irc);
    static uint8_t buf[0x1000];
    int ids[2] = {0xf010, 0xf00c};
    for (int i = 0; i < 2; i++) {
        memset(buf, 0, sizeof(buf));
        int rc = get(ids[i], buf);
        klog_printf("[ifusb] task %x rc=0x%08x\n", ids[i], rc);
    }
    klog_printf("[ifusb] bgft probe done\n");
    return 0;
}
