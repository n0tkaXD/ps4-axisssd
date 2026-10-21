/* Host test: runs the ps4mount generators on a PKG and writes the files a PS4
 * install leaves, so they can be compared byte for byte with a real install.
 * Usage: gen_test <pkg> <url as the console saw it> <out_dir> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "build.h"
#include "pkg.h"

static void save(const char *dir, const char *name, const void *d, size_t n) {
    char p[1024];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    FILE *f = fopen(p, "wb");
    if (!f || fwrite(d, 1, n, f) != n) {
        perror(p);
        exit(1);
    }
    fclose(f);
}

static void save_entry(pkg_t *pkg, uint32_t id, const char *dir, const char *name) {
    size_t n;
    uint8_t *d = pkg_entry(pkg, id, &n);
    if (!d) {
        printf("entry 0x%x (%s): absent\n", id, name);
        return;
    }
    save(dir, name, d, n);
    free(d);
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s pkg url out_dir\n", argv[0]);
        return 2;
    }
    pkg_t pkg;
    if (pkg_open(&pkg, argv[1])) {
        fprintf(stderr, "pkg_open failed\n");
        return 1;
    }
    mkdir(argv[3], 0755);
    printf("content_id %s title_id %s fake_key %d\n", pkg.content_id, pkg.title_id, pkg_fake_key(&pkg));

    size_t sl;
    uint8_t *sfo_raw = pkg_entry(&pkg, 0x1000, &sl);
    sfo_t sfo;
    const char *app_ver = "01.00";
    if (sfo_raw && !sfo_parse(sfo_raw, sl, &sfo)) {
        const sfo_item_t *v = sfo_get(&sfo, "APP_VER");
        if (v && !v->is_int)
            app_ver = v->sval;
    }
    printf("APP_VER %s\n", app_ver);

    buf_t json = {0}, pbm = {0}, xml = {0}, ppbm = {0}, rif = {0}, idx = {0};
    build_app_json(&json, &pkg, argv[2]);
    save(argv[3], "app.json", json.data, json.len);
    build_app_pbm(&pbm, &pkg, app_ver, PBM_APP);
    save(argv[3], "app.pbm", pbm.data, pbm.len);

    size_t pl;
    uint8_t *pg = pkg_entry(&pkg, 0x1001, &pl);
    if (build_app_xml(&xml, pg, pl) == 0)
        save(argv[3], "app.xml", xml.data, xml.len);
    else
        printf("app.xml: playgo-chunk.dat unusable\n");
    if (build_pfs_pbm(&ppbm, &pkg, pg, pl, NULL) == 0)
        save(argv[3], "pfs.pbm", ppbm.data, ppbm.len);
    else
        printf("pfs.pbm: failed\n");

    size_t ll;
    uint8_t *lic = pkg_entry(&pkg, 0x400, &ll);
    if (lic && ll == 0x400) {
        build_license(&rif, &idx, NULL, 0, NULL, 0, pkg.content_id, lic);
        save(argv[3], "license.rif", rif.data, rif.len);
        save(argv[3], "license.idx", idx.data, idx.len);
    } else {
        printf("license.dat: absent or wrong size (%zu)\n", ll);
    }

    save_entry(&pkg, 0x1000, argv[3], "param.sfo");
    save_entry(&pkg, 0x1200, argv[3], "icon0.png");
    save_entry(&pkg, 0x1220, argv[3], "pic0.png");
    save_entry(&pkg, 0x1006, argv[3], "pic1.png");
    save_entry(&pkg, 0x12a0, argv[3], "pic0.dds");
    save_entry(&pkg, 0x402, argv[3], "nptitle.dat");
    save_entry(&pkg, 0x403, argv[3], "npbind.dat");
    pkg_close(&pkg);
    return 0;
}
