/* Host check: place_files() into a temp root must reproduce the real install
 * captured in snapshots/after byte for byte (except the system-made DDS files).
 * Usage: host_test <local pkg> <pkg path on console> <temp root> <snapshot files dir> */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "place.h"

static void logf_(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("  ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

static int same(const char *a, const char *b) {
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    int ok = fa && fb;
    while (ok) {
        int ca = fgetc(fa), cb = fgetc(fb);
        if (ca != cb)
            ok = 0;
        if (ca == EOF || cb == EOF)
            break;
    }
    if (fa)
        fclose(fa);
    if (fb)
        fclose(fb);
    return ok;
}

int main(int argc, char **argv) {
    if (argc != 5)
        return 2;
    place_t p = {.root = argv[3], .pkg_path = argv[2], .pkg_open_path = argv[1],
                 .language_mask = place_language_mask(8), .log = logf_};
    if (place_files(&p)) {
        printf("place_files FAILED\n");
        return 1;
    }
    const char *files[] = {
        "/user/app/%s/app.json", "/user/app/%s/app.pbm", "/user/app/%s/app.pbm.backup", "/user/app/%s/app.xml",
        "/system_data/playgo/%s/pfs.pbm", "/system_data/playgo/%s/pfs.pbm.backup",
        "/system_data/priv/appmeta/%s/param.sfo", "/system_data/priv/appmeta/%s/ext_info.dat",
        "/user/appmeta/%s/icon0.png", "/user/appmeta/%s/pic0.png", "/user/appmeta/%s/pic1.png", NULL};
    int bad = 0;
    char rel[256], a[1024], b[1024];
    for (int i = 0; files[i]; i++) {
        snprintf(rel, sizeof(rel), files[i], p.title_id);
        snprintf(a, sizeof(a), "%s%s", argv[3], rel);
        snprintf(b, sizeof(b), "%s%s", argv[4], rel);
        int ok = same(a, b);
        bad += !ok;
        printf("%s %s\n", ok ? "SAME" : "DIFF", rel);
    }
    snprintf(rel, sizeof(rel), "/user/license/free%.19s", p.content_id);
    const char *ext[] = {".rif", ".idx"};
    for (int i = 0; i < 2; i++) {
        snprintf(a, sizeof(a), "%s%s%s", argv[3], rel, ext[i]);
        snprintf(b, sizeof(b), "%s%s%s", argv[4], rel, ext[i]);
        int ok = same(a, b);
        bad += !ok;
        printf("%s %s%s\n", ok ? "SAME" : "DIFF", rel, ext[i]);
    }
    printf(bad ? "%d file(s) differ\n" : "all files match\n", bad);
    return bad != 0;
}
