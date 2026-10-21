/* BGFT task record (/user/bgft/task/<id>/d0.pdb, d1.pdb) for a title whose
 * package is already in place. Games (category gd) need one: without it
 * sceBgftNotifyGameWillStart fails with 0x80990019 (PLAN 12.2). Layout read
 * from real PS4 12.00 tasks. */
#pragma once

#include <stdint.h>

#include "build.h"
#include "pkg.h"

typedef struct {
    uint32_t task_id;     /* folder name; record 0x1a4 holds task_id - 1 */
    uint32_t fw_raw;      /* kernel_get_fw_version(), record 0x95 */
    uint64_t created, updated; /* records 0x8b and 0x76 (same clock as real tasks) */
    const char *src_url;  /* record 0x1388: where the package lives */
    const char *name;     /* record 0x69: shown name (real installs use the file name) */
} bgft_task_t;

/* app_json: the same bytes written to /user/app/<TID>/app.json. */
int build_bgft_task(buf_t *out, const pkg_t *pkg, const uint8_t *playgo, size_t playgo_len,
                    const buf_t *app_json, const bgft_task_t *t);
