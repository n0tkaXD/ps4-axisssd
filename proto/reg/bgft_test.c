/* Host check: rebuild a real BGFT task from its package and compare.
 * Usage: bgft_test <pkg> <src_url> <name> <real d0.pdb> <task_id hex> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bgft.h"
static uint64_t field64(const uint8_t *d, size_t n, uint32_t key) {
    size_t off = 4;
    while (off + 12 <= n) {
        uint32_t k = d[off] | d[off+1] << 8 | d[off+2] << 16 | (uint32_t)d[off+3] << 24;
        uint32_t l = d[off+4] | d[off+5] << 8 | d[off+6] << 16 | (uint32_t)d[off+7] << 24;
        if (k == key && l == 8) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = v << 8 | d[off+12+i]; return v; }
        off += 12 + l;
    }
    return 0;
}
int main(int c, char **v) {
    if (c != 6) return 2;
    pkg_t p; if (pkg_open(&p, v[1])) { puts("pkg_open failed"); return 1; }
    FILE *f = fopen(v[4], "rb"); uint8_t real[65536]; size_t n = fread(real, 1, sizeof(real), f); fclose(f);
    size_t pl; uint8_t *pg = pkg_entry(&p, 0x1001, &pl);
    buf_t json = {0}, rec = {0};
    build_app_json(&json, &p, v[2]);
    bgft_task_t t = {.task_id = (uint32_t)strtoul(v[5], NULL, 16), .fw_raw = 0x12008011,
                     .created = field64(real, n, 0x8b), .updated = field64(real, n, 0x76), .src_url = v[2], .name = v[3]};
    if (build_bgft_task(&rec, &p, pg, pl, &json, &t)) { puts("build failed"); return 1; }
    int same = rec.len == n && !memcmp(rec.data, real, n);
    printf("generated %zu bytes, real %zu bytes: %s\n", rec.len, n, same ? "IDENTICAL" : "DIFFERENT");
    if (!same) { FILE *o = fopen("proto/build/bgft_gen.pdb", "wb"); fwrite(rec.data, 1, rec.len, o); fclose(o); }
    return !same;
}
