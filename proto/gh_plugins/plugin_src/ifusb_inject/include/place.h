/* Writes the files a PS4 12.00 fPKG install leaves behind, with app.pkg as a
 * symlink to the package instead of a copy. Formats were checked byte for byte
 * against a real install (PLAN_install_to_usb.md, section 7). */
#pragma once

#include <stdint.h>

typedef void (*place_log_fn)(const char *fmt, ...);

typedef struct {
    const char *root;     /* "" on the console; a temp folder for host tests */
    const char *pkg_path; /* package as the console sees it (symlink target, app.json url) */
    const char *pkg_open_path; /* where to read it from; NULL = pkg_path (host tests differ) */
    uint64_t language_mask;
    place_log_fn log;
    /* filled by place_files */
    char title_id[10];
    char content_id[37];
} place_t;

/* 1 << (63 - system language), as found in app.xml of real installs. */
uint64_t place_language_mask(int system_language);

/* Reads title_id/content_id from the package; writes nothing. 0 on success. */
int place_read_ids(place_t *p);

/* Returns 0 when every file was written. */
int place_files(place_t *p);

/* Makes sure a BGFT task exists for the title (games need one to start).
 * Reuses a task that already names the title; otherwise writes
 * /user/bgft/task/<id>/d0.pdb and d1.pdb with id from 0xf001 up.
 * unix_time: seconds since 1970 (UTC). Returns the task id, or 0 on error. */
uint32_t place_bgft_task(place_t *p, uint32_t fw_raw, int64_t unix_time);

/* Removes what place_files wrote (license slot excluded). */
void place_remove(place_t *p);
