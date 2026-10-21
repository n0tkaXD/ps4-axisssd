/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * The app_* calls of the web panel, recorded instead of done, for the host tests. */
#include <stdio.h>
#include <string.h>

#include "stubs.h"
#include "web.h"

char g_stub_call[64], g_stub_arg[4096];
int g_stub_calls;

static int record(const char *call, const char *arg, char *msg, size_t cap) {
    snprintf(g_stub_call, sizeof(g_stub_call), "%s", call);
    snprintf(g_stub_arg, sizeof(g_stub_arg), "%s", arg ? arg : "(null)");
    g_stub_calls++;
    snprintf(msg, cap, "done \"%s\"", call);
    return 0;
}

void app_games_json(buf_t *out) { buf_printf(out, "{\"version\":\"test\",\"games\":[]}"); }
int app_add(const char *tid, char *msg, size_t cap) { return record("add", tid, msg, cap); }
int app_remove(const char *tid, char *msg, size_t cap) { return record("remove", tid, msg, cap); }
void app_rescan(void) {
    char m[8];
    record("rescan", NULL, m, sizeof(m));
}
void app_stop(void) {
    char m[8];
    record("stop", NULL, m, sizeof(m));
}
int app_restore(const char *tids, char *msg, size_t cap) { return record("restore", tids, msg, cap); }
int app_restored_forget(char *msg, size_t cap) { return record("forget", NULL, msg, cap); }
void app_config_json(buf_t *out) { buf_printf(out, "{\"batch\":true}"); }
int app_config_set(const char *body, char *msg, size_t cap) { return record("config", body, msg, cap); }
int app_icon(const char *tid, buf_t *out) {
    char m[8];
    record("icon", tid, m, sizeof(m));
    buf_put(out, "PNG", 3);
    return 0;
}
