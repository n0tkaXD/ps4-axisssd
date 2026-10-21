/* SPDX-License-Identifier: GPL-3.0-or-later
 * BGFT task record writer, see bgft.h. Record order and constant values copy
 * real single-package tasks on PS4 12.00 (e.g. /user/bgft/task/00000121). */
#include "bgft.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void rec(buf_t *b, uint32_t key, const void *data, uint32_t len) {
    uint8_t h[12];
    for (int i = 0; i < 4; i++) {
        h[i] = (uint8_t)(key >> (8 * i));
        h[4 + i] = h[8 + i] = (uint8_t)(len >> (8 * i));
    }
    buf_put(b, h, 12);
    if (len)
        buf_put(b, data, len);
}

static void rec32(buf_t *b, uint32_t key, uint32_t v) {
    uint8_t d[4];
    for (int i = 0; i < 4; i++)
        d[i] = (uint8_t)(v >> (8 * i));
    rec(b, key, d, 4);
}

static void rec64(buf_t *b, uint32_t key, uint64_t v) {
    uint8_t d[8];
    for (int i = 0; i < 8; i++)
        d[i] = (uint8_t)(v >> (8 * i));
    rec(b, key, d, 8);
}

static void recstr(buf_t *b, uint32_t key, const char *s) { rec(b, key, s, (uint32_t)strlen(s)); }

int build_bgft_task(buf_t *out, const pkg_t *pkg, const uint8_t *playgo, size_t playgo_len,
                    const buf_t *app_json, const bgft_task_t *t) {
    uint16_t chunks = playgo_chunk_count(playgo, playgo_len);
    if (!chunks)
        return -1;
    uint8_t *status = malloc(chunks), *list = malloc(chunks * 4u);
    if (!status || !list) {
        free(status);
        free(list);
        return -1;
    }
    for (unsigned i = 0; i < chunks; i++) {
        status[i] = 3; /* chunk fully present */
        list[i * 4] = (uint8_t)i;
        list[i * 4 + 1] = (uint8_t)(i >> 8);
        list[i * 4 + 2] = 3;
        list[i * 4 + 3] = 0;
    }
    char tid[10], dest[64];
    snprintf(tid, sizeof(tid), "%.9s", pkg->title_id);
    snprintf(dest, sizeof(dest), "/user/app/%s/app.pkg", tid);

    static const uint8_t zero4[4] = {0}, ff4[4] = {0xff, 0xff, 0xff, 0xff};
    uint32_t hdr = 0;
    buf_put(out, &hdr, 4);
    rec32(out, 0x95, t->fw_raw);
    rec64(out, 0x8b, t->created);
    rec32(out, 0x64, 3); /* task type 3 */
    rec32(out, 0x6b, 6); /* subtype 6 */
    rec(out, 0x65, zero4, 4);
    rec(out, 0x78, zero4, 4);
    rec(out, 0x66, zero4, 4);
    rec(out, 0x68, zero4, 4);
    rec64(out, 0x6c, 0);
    rec32(out, 0x6d, 2);
    rec64(out, 0x76, t->updated);
    rec32(out, 0x190, 1);
    rec(out, 0x74, pkg->content_id, 36);
    rec(out, 0x7c, tid, 9);
    rec32(out, 0x19a, 4);
    rec(out, 0x75, zero4, 4);
    rec(out, 0x192, "0", 1);
    rec64(out, 0x193, 0x0080000000000000ull); /* bytes 00..00 80 00 as in real tasks */
    rec32(out, 0x70, 2);
    rec(out, 0x19d, zero4, 4);
    rec(out, 0x195, status, chunks);
    rec(out, 0x1390, app_json->data, (uint32_t)app_json->len);
    recstr(out, 0x1388, t->src_url);
    recstr(out, 0x1389, dest);
    rec(out, 0x194, status, chunks);
    rec32(out, 0x1f4, 1); /* one package: no patch */
    rec(out, 0x85, NULL, 0);
    recstr(out, 0x69, t->name);
    rec64(out, 0x1a3, pkg->pkg_size);
    rec64(out, 0x7a, pkg->pkg_size);
    rec32(out, 0x1a4, t->task_id - 1);
    rec(out, 0xb4, NULL, 0);
    rec(out, 0xb5, NULL, 0);
    rec(out, 0xb6, ff4, 4);
    rec(out, 0xb7, ff4, 4);
    rec(out, 0xbb, NULL, 0);
    rec(out, 0xb8, ff4, 4);
    rec(out, 0xb9, NULL, 0);
    rec64(out, 0x9a, 0);
    rec(out, 0x191, list, chunks * 4u);
    rec64(out, 0x1a6, 0x0080000000000000ull);
    rec64(out, 0x7b, pkg->pkg_size);
    free(status);
    free(list);
    return 0;
}
