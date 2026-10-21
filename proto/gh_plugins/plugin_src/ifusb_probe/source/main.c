/* ifusb_probe: GoldHEN plugin that only reports where it runs.
 * Shows one notification and writes one klog line. Changes nothing else. */
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include "plugin_common.h"

attr_public const char *g_pluginName = "ifusb_probe";
attr_public const char *g_pluginDesc = "Reports the process it loads into";
attr_public const char *g_pluginAuth = "ifusb";
attr_public uint32_t g_pluginVersion = 0x00000100;

int32_t attr_public plugin_load(int32_t argc, const char *argv[]) {
    (void)argc;
    (void)argv;
    final_printf("[ifusb] probe loaded, pid=%d\n", (int)getpid());
    char msg[128] = {0};
    snprintf(msg, sizeof(msg), "ifusb probe: pid %d", (int)getpid());
    NotifyStatic(TEX_ICON_SYSTEM, msg);
    return 0;
}

int32_t attr_public plugin_unload(int32_t argc, const char *argv[]) {
    (void)argc;
    (void)argv;
    final_printf("[ifusb] probe unloaded\n");
    return 0;
}

s32 attr_module_hidden module_start(s64 argc, const void *args) { (void)argc; (void)args; return 0; }
s32 attr_module_hidden module_stop(s64 argc, const void *args) { (void)argc; (void)args; return 0; }
