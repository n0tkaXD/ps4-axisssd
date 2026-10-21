/* ifusb_loader, injection test: asks GoldHEN to load ifusb_inject.prx into the
 * ScePartyDaemon system process and shows the result. Registers nothing. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "GoldHEN.h"
#include "plugin_common.h"

attr_public const char *g_pluginName = "ifusb_loader";
attr_public const char *g_pluginDesc = "Loads PS4 packages from the PKG folder of USB drives";
attr_public const char *g_pluginAuth = "ifusb";
attr_public uint32_t g_pluginVersion = 0x00000100;

int32_t attr_public plugin_load(int32_t argc, const char *argv[]) {
    (void)argc;
    (void)argv;
    char proc[] = "ScePartyDaemon";
    char prx[] = "/data/GoldHEN/plugins/ifusb_inject_l.prx";
    int rc = sys_sdk_proc_prx_load(proc, prx);
    char msg[160] = {0};
    snprintf(msg, sizeof(msg), "ifusb: inject rc=%d", rc);
    NotifyStatic(TEX_ICON_SYSTEM, msg);
    return 0;
}

int32_t attr_public plugin_unload(int32_t argc, const char *argv[]) {
    (void)argc;
    (void)argv;
    return 0;
}

s32 attr_module_hidden module_start(s64 argc, const void *args) { (void)argc; (void)args; return 0; }
s32 attr_module_hidden module_stop(s64 argc, const void *args) { (void)argc; (void)args; return 0; }
