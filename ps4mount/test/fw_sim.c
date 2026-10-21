/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Firmware simulation: the registration method PS4Mount picks on every firmware it could
 * run on, with and without sceAppInstUtilAppInstallTitleDir. Run with: bash test/run.sh
 * With --report, also prints a markdown table of the PS5 releases after the results. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fw.h"

static int g_failed, g_checks;

#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        g_checks++;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            g_failed++;                                                                                                \
            printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);                                                   \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

/* kernel_get_fw_version() value for major.minor (BCD, low half as on 13.20: 0x13200006). */
static uint32_t raw_fw(unsigned major, unsigned minor, unsigned low) {
    return (uint32_t)(major / 10) << 28 | (uint32_t)(major % 10) << 24 | (uint32_t)(minor / 10) << 20 |
           (uint32_t)(minor % 10) << 16 | (low & 0xffff);
}

static const char *mode_name(int m) {
    return m == REG_ALL ? "AppInstallAll" : m == REG_TITLEDIR ? "AppInstallTitleDir" : "none (no games added)";
}

/* PS5 system software releases (the sweep below covers every version anyway). */
static const char *const k_releases[] = {
    "1.00",  "1.02",  "1.05",  "1.10",  "1.12",  "1.14",  "2.00",  "2.20",  "2.25",  "2.26",  "2.30",  "2.50",
    "2.70",  "3.00",  "3.10",  "3.20",  "3.21",  "4.00",  "4.02",  "4.03",  "4.50",  "4.51",  "5.00",  "5.02",
    "5.10",  "5.50",  "6.00",  "6.02",  "6.50",  "7.00",  "7.01",  "7.20",  "7.40",  "7.60",  "7.61",  "8.00",
    "8.20",  "8.40",  "8.60",  "9.00",  "9.05",  "9.20",  "9.40",  "9.60",  "10.00", "10.01", "10.20", "10.40",
    "10.60", "11.00", "11.20", "11.40", "11.60", "12.00", "12.02", "12.20", "12.40", "12.60", "13.00", "13.20",
    NULL};

int main(int argc, char **argv) {
    int report = argc > 1 && !strcmp(argv[1], "--report");
    printf("firmware sweep 0.00 to 99.99\n");
    for (unsigned major = 0; major <= 99; major++)
        for (unsigned minor = 0; minor <= 99; minor++)
            for (int td = 0; td <= 1; td++) {
                uint32_t raw = raw_fw(major, minor, major * 7 + minor); /* any low half */
                unsigned ma, mi;
                char text[16], want[16];
                fw_decode(raw, &ma, &mi);
                CHECK(ma == major && mi == minor, "0x%08x decoded as %u.%u", raw, ma, mi);
                fw_text(raw, text, sizeof(text));
                snprintf(want, sizeof(want), "%u.%02u", major, minor);
                CHECK(!strcmp(text, want), "0x%08x shown as %s", raw, text);
                int m = fw_reg_mode(ma, td);
                if (major >= MIN_FW_INSTALL_ALL)
                    CHECK(m == REG_ALL, "%s with TitleDir %d: %s", text, td, mode_name(m));
                else
                    /* AppInstallAll corrupts the database below 6.00 (issues #11, #13): never used there */
                    CHECK(m == (td ? REG_TITLEDIR : REG_NONE), "%s with TitleDir %d: %s", text, td, mode_name(m));
                CHECK(major >= MIN_FW_INSTALL_ALL || m != REG_ALL, "AppInstallAll chosen on %s", text);
            }

    printf("values seen on consoles\n");
    char text[16];
    fw_text(0x13200006, text, sizeof(text));
    CHECK(!strcmp(text, "13.20"), "%s", text);
    fw_text(0x04500005, text, sizeof(text));
    CHECK(!strcmp(text, "4.50"), "%s", text);
    fw_text(0x05500000, text, sizeof(text));
    CHECK(!strcmp(text, "5.50"), "%s", text);
    CHECK(fw_reg_mode(13, 0) == REG_ALL, "13.20 without TitleDir");  /* PS4Mount test console */
    CHECK(fw_reg_mode(5, 1) == REG_TITLEDIR, "5.50 with TitleDir");   /* issue #11 */
    CHECK(fw_reg_mode(4, 1) == REG_TITLEDIR, "4.50 with TitleDir");   /* issue #13 */
    CHECK(fw_reg_mode(5, 0) == REG_NONE, "5.50 without TitleDir");

    FILE *f = report ? stdout : NULL;
    if (f)
        fprintf(f, "### Registration method per firmware\n\n| Firmware | With AppInstallTitleDir | Without it |\n|---|---|---|\n");
    for (int i = 0; k_releases[i]; i++) {
        unsigned major = (unsigned)atoi(k_releases[i]), minor = (unsigned)atoi(strchr(k_releases[i], '.') + 1);
        unsigned ma, mi;
        fw_decode(raw_fw(major, minor, 0), &ma, &mi);
        int with = fw_reg_mode(ma, 1), without = fw_reg_mode(ma, 0);
        CHECK(ma == major && mi == minor, "%s", k_releases[i]);
        if (f)
            fprintf(f, "| %s | %s | %s |\n", k_releases[i], mode_name(with), mode_name(without));
    }

    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed != 0;
}
