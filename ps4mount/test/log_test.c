/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * The log with the time on every line. Run with: bash test/run.sh */
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "logts.h"

static int g_failed, g_checks;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        g_checks++;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            g_failed++;                                                                                                \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                          \
        }                                                                                                              \
    } while (0)

static char g_out[65536];

/* Feeds the chunks to log_write_lines with a fixed stamp and returns what was written. */
static const char *feed(const char *const *chunks) {
    FILE *f = tmpfile();
    int at_start = 1;
    for (int i = 0; chunks[i]; i++)
        log_write_lines(fileno(f), &at_start, chunks[i], strlen(chunks[i]), "T ");
    rewind(f);
    size_t n = fread(g_out, 1, sizeof(g_out) - 1, f);
    g_out[n] = 0;
    fclose(f);
    return g_out;
}

static int matches(const char *re, const char *s) {
    regex_t r;
    if (regcomp(&r, re, REG_EXTENDED | REG_NOSUB))
        return 0;
    int ok = !regexec(&r, s, 0, NULL, 0);
    regfree(&r);
    return ok;
}

int main(void) {
    fprintf(stderr, "lines\n");
    CHECK(!strcmp(feed((const char *[]){"a\nb\n", NULL}), "T a\nT b\n"));
    CHECK(!strcmp(feed((const char *[]){"one line, ", "two reads\n", NULL}), "T one line, two reads\n"));
    CHECK(!strcmp(feed((const char *[]){"x\n", "\n", "y", NULL}), "T x\nT \nT y"));
    CHECK(!strcmp(feed((const char *[]){"", NULL}), ""));

    fprintf(stderr, "time\n");
    char stamp[40];
    log_stamp(stamp, sizeof(stamp));
    CHECK(matches("^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{3} $", stamp));

    fprintf(stderr, "stdout to the log file\n");
    char path[] = "/tmp/ps4mount_log_XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    close(fd);
    int saved = dup(STDOUT_FILENO);
    CHECK(!log_start(path));
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("[main] first\n");
    printf("[scan] second, ");
    printf("same line\n");
    for (int i = 0; i < 2000; i++)
        printf("[klog] line %d\n", i);
    log_stop();
    log_stop(); /* twice is harmless */
    dup2(saved, STDOUT_FILENO);
    close(saved);
    FILE *f = fopen(path, "r");
    char line[256];
    int n = 0, stamped = 0, first = 0, joined = 0, last = 0;
    while (f && fgets(line, sizeof(line), f)) {
        n++;
        stamped += matches("^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{3} \\[", line);
        first |= strstr(line, "[main] first\n") != NULL;
        joined |= strstr(line, "[scan] second, same line\n") != NULL;
        last |= strstr(line, "[klog] line 1999\n") != NULL;
    }
    if (f)
        fclose(f);
    unlink(path);
    CHECK(n == 2002);
    CHECK(stamped == n);
    CHECK(first && joined);
    CHECK(last); /* nothing lost at the end */

    fprintf(stderr, "%d checks, %d failed\n", g_checks, g_failed);
    return g_failed != 0;
}
