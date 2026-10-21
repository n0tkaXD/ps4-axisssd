/* SPDX-License-Identifier: GPL-3.0-or-later
 * Install-file layout for PS4 12.00, built on the ps4mount generators
 * (build.c / pkg.c, Copyright (C) 2026 gasb07). */
#include "place.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bgft.h"
#include "build.h"
#include "pkg.h"

/* Artwork copied to /user/appmeta/<TID> (same list as ps4mount). */
static const struct {
    uint32_t id;
    const char *name;
} k_meta[] = {
    {0x1200, "icon0.png"}, {0x1220, "pic0.png"},  {0x1006, "pic1.png"},          {0x1240, "snd0.at9"},
    {0x1280, "icon0.dds"}, {0x12a0, "pic0.dds"},  {0x1004, "pronunciation.xml"}, {0x100b, "shareparam.json"},
    {0, NULL},
};

/* Same 44 bytes in /system_data/priv/appmeta/<TID>/ext_info.dat for all 18
 * titles on the test console. */
static const uint8_t k_ext_info[44] = {1, 0, 0, 0, 0, 0, 0, 0, 1};

uint64_t place_language_mask(int lang) {
    if (lang < 0 || lang > 63)
        lang = 1; /* English (US) */
    return 1ull << (63 - lang);
}

/* FreeBSD syscall 57 = symlink. The toolchain libc version blocks inside the
 * system process, so the call is made directly. */
extern long syscall(long number, ...);
static int raw_symlink(const char *target, const char *link) {
    return (int)syscall(57, target, link);
}

static void mkpath(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *s = tmp + 1; *s; s++)
        if (*s == '/') {
            *s = 0;
            mkdir(tmp, mode);
            *s = '/';
        }
    mkdir(tmp, mode);
}

static int put(place_t *p, const char *rel, const void *data, size_t len, mode_t mode) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s%s", p->root, rel);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) {
        p->log("open %s: %s", path, strerror(errno));
        return -1;
    }
    ssize_t n = write(fd, data, len);
    close(fd);
    chmod(path, mode);
    if (n != (ssize_t)len) {
        p->log("write %s: %s", path, strerror(errno));
        return -1;
    }
    p->log("wrote %s (%zu bytes)", path, len);
    return 0;
}

static int put_buf(place_t *p, const char *rel, const buf_t *b) { return put(p, rel, b->data, b->len, 0777); }

/* Copies a pkg entry; an absent entry is not an error. */
static int put_entry(place_t *p, pkg_t *pkg, uint32_t id, const char *rel) {
    size_t len;
    uint8_t *d = pkg_entry(pkg, id, &len);
    if (!d)
        return 0;
    int rc = put(p, rel, d, len, 0777);
    free(d);
    return rc;
}

static uint8_t *read_all(const char *path, size_t *len) {
    struct stat st;
    *len = 0;
    if (stat(path, &st) || st.st_size <= 0 || st.st_size > (1 << 20))
        return NULL;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    uint8_t *d = malloc((size_t)st.st_size);
    if (d && read(fd, d, (size_t)st.st_size) != st.st_size) {
        free(d);
        d = NULL;
    }
    close(fd);
    if (d)
        *len = (size_t)st.st_size;
    return d;
}

static int place_license(place_t *p, pkg_t *pkg) {
    size_t ll, rl, il;
    uint8_t *lic = pkg_entry(pkg, 0x400, &ll);
    if (!lic || ll != 0x400) {
        free(lic);
        p->log("no license.dat in the package");
        return -1;
    }
    char rrel[96], irel[96], rpath[PATH_MAX], ipath[PATH_MAX];
    /* PS4 names them free<CID[:19]>; ps4mount (PS5) uses fake<...>. */
    snprintf(rrel, sizeof(rrel), "/user/license/free%.19s.rif", pkg->content_id);
    snprintf(irel, sizeof(irel), "/user/license/free%.19s.idx", pkg->content_id);
    snprintf(rpath, sizeof(rpath), "%s%s", p->root, rrel);
    snprintf(ipath, sizeof(ipath), "%s%s", p->root, irel);
    uint8_t *old_rif = read_all(rpath, &rl), *old_idx = read_all(ipath, &il);
    buf_t rif = {0}, idx = {0};
    build_license(&rif, &idx, old_rif, rl, old_idx, il, pkg->content_id, lic);
    int rc = put(p, rrel, rif.data, rif.len, 0770) | put(p, irel, idx.data, idx.len, 0770);
    free(lic);
    free(old_rif);
    free(old_idx);
    buf_free(&rif);
    buf_free(&idx);
    return rc;
}

/* Title and content IDs come from the package file: reject anything that is not
 * the PS4 format, so they cannot carry path separators into file names. */
static int valid_tid(const char *t) {
    for (int i = 0; i < 4; i++)
        if (t[i] < 'A' || t[i] > 'Z') return 0;
    for (int i = 4; i < 9; i++)
        if (t[i] < '0' || t[i] > '9') return 0;
    return t[9] == 0;
}

static int valid_cid(const char *c) {
    size_t n = strlen(c);
    if (n != 36) return 0;
    for (size_t i = 0; i < n; i++) {
        char ch = c[i];
        int ok = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        if (!ok) return 0;
    }
    return c[16] == '_' && c[17] == '0' && c[18] == '0' && c[19] == '-';
}

int place_read_ids(place_t *p) {
    pkg_t pkg;
    const char *open_path = p->pkg_open_path ? p->pkg_open_path : p->pkg_path;
    if (pkg_open(&pkg, open_path))
        return -1;
    int fake = pkg_fake_key(&pkg);
    snprintf(p->title_id, sizeof(p->title_id), "%s", pkg.title_id);
    snprintf(p->content_id, sizeof(p->content_id), "%s", pkg.content_id);
    pkg_close(&pkg);
    if (!valid_tid(p->title_id) || !valid_cid(p->content_id)) {
        p->title_id[0] = 0;
        return -1;
    }
    return fake ? 0 : -1;
}

int place_files(place_t *p) {
    pkg_t pkg;
    char rel[PATH_MAX], path[PATH_MAX];
    int rc = 0;

    const char *open_path = p->pkg_open_path ? p->pkg_open_path : p->pkg_path;
    if (pkg_open(&pkg, open_path)) {
    p->log("pf pkg_open");
        p->log("cannot open package %s", open_path);
        return -1;
    }
    if (!pkg_fake_key(&pkg)) {
        p->log("%s is not a fake PKG", p->pkg_path);
        pkg_close(&pkg);
        return -1;
    }
    snprintf(p->title_id, sizeof(p->title_id), "%s", pkg.title_id);
    snprintf(p->content_id, sizeof(p->content_id), "%s", pkg.content_id);
    const char *tid = p->title_id;
    p->log("package %s: %s", p->pkg_path, p->content_id);

    /* /user/app/<TID>: app.pkg is a link to the package, not a copy */
    snprintf(path, sizeof(path), "%s/user/app/%s", p->root, tid);
    mkpath(path, 0700);
    p->log("pf mkpath app");
    snprintf(path, sizeof(path), "%s/user/app/%s/app.pkg", p->root, tid);
    p->log("pf before unlink %s", path);
    unlink(path);
    p->log("pf after unlink");
    p->log("pf calling raw symlink");
    int slr = symlink(p->pkg_path, path);
    p->log("pf raw symlink rc=%d errno=%d", slr, errno);
    if (slr) {
    p->log("pf symlink");
        p->log("pf symlink failed");
        pkg_close(&pkg);
        return -1;
    }
    p->log("linked %s -> %s", path, p->pkg_path);

    size_t pl = 0;
    uint8_t *pg = pkg_entry(&pkg, 0x1001, &pl);
    buf_t json = {0}, pbm = {0}, xml = {0}, ppbm = {0};
    build_app_json(&json, &pkg, p->pkg_path);
    p->log("pf build json");
    build_app_pbm(&pbm, &pkg, "", 0); /* PS4: empty version, type 0 */
    if (build_app_xml(&xml, pg, pl, p->language_mask) || build_pfs_pbm(&ppbm, &pkg, pg, pl, NULL)) {
        p->log("playgo-chunk.dat missing or unusable");
        rc = -1;
        goto out;
    }
    snprintf(rel, sizeof(rel), "/user/app/%s/app.json", tid);
    rc |= put_buf(p, rel, &json);
    snprintf(rel, sizeof(rel), "/user/app/%s/app.pbm", tid);
    rc |= put_buf(p, rel, &pbm);
    p->log("pf put pbm");
    snprintf(rel, sizeof(rel), "/user/app/%s/app.pbm.backup", tid);
    rc |= put_buf(p, rel, &pbm);
    p->log("pf put pbm");
    snprintf(rel, sizeof(rel), "/user/app/%s/app.xml", tid);
    rc |= put_buf(p, rel, &xml);

    snprintf(path, sizeof(path), "%s/system_data/playgo/%s", p->root, tid);
    mkpath(path, 0770);
    p->log("pf mkpath playgo");
    snprintf(rel, sizeof(rel), "/system_data/playgo/%s/pfs.pbm", tid);
    rc |= put_buf(p, rel, &ppbm);
    snprintf(rel, sizeof(rel), "/system_data/playgo/%s/pfs.pbm.backup", tid);
    rc |= put_buf(p, rel, &ppbm);

    snprintf(path, sizeof(path), "%s/system_data/priv/appmeta/%s", p->root, tid);
    mkpath(path, 0700);
    p->log("pf mkpath app");
    snprintf(rel, sizeof(rel), "/system_data/priv/appmeta/%s/param.sfo", tid);
    rc |= put_entry(p, &pkg, 0x1000, rel);
    p->log("pf put sfo");
    snprintf(rel, sizeof(rel), "/system_data/priv/appmeta/%s/nptitle.dat", tid);
    rc |= put_entry(p, &pkg, 0x402, rel);
    snprintf(rel, sizeof(rel), "/system_data/priv/appmeta/%s/npbind.dat", tid);
    rc |= put_entry(p, &pkg, 0x403, rel);
    snprintf(rel, sizeof(rel), "/system_data/priv/appmeta/%s/ext_info.dat", tid);
    rc |= put(p, rel, k_ext_info, sizeof(k_ext_info), 0777);

    snprintf(path, sizeof(path), "%s/user/appmeta/%s", p->root, tid);
    mkpath(path, 0700);
    p->log("pf mkpath app");
    for (int i = 0; k_meta[i].name; i++) {
        snprintf(rel, sizeof(rel), "/user/appmeta/%s/%s", tid, k_meta[i].name);
        rc |= put_entry(p, &pkg, k_meta[i].id, rel);
    }

    snprintf(path, sizeof(path), "%s/user/license", p->root);
    mkpath(path, 0770);
    p->log("pf mkpath playgo");
    rc |= place_license(p, &pkg);
    p->log("pf license");

out:
    free(pg);
    buf_free(&json);
    buf_free(&pbm);
    buf_free(&xml);
    buf_free(&ppbm);
    pkg_close(&pkg);
    return rc ? -1 : 0;
}

/* Title ID stored in a task record (key 0x7c), or "" when unreadable. */
static void task_title(const char *path, char out[10]) {
    size_t n;
    uint8_t *d = read_all(path, &n);
    out[0] = 0;
    for (size_t off = 4; d && off + 12 <= n;) {
        uint32_t k = d[off] | d[off + 1] << 8 | d[off + 2] << 16 | (uint32_t)d[off + 3] << 24;
        uint32_t l = d[off + 4] | d[off + 5] << 8 | d[off + 6] << 16 | (uint32_t)d[off + 7] << 24;
        if (off + 12 + l > n)
            break;
        if (k == 0x7c && l == 9) {
            memcpy(out, d + off + 12, 9);
            out[9] = 0;
            break;
        }
        off += 12 + l;
    }
    free(d);
}

uint32_t place_bgft_task(place_t *p, uint32_t fw_raw, int64_t unix_time) {
    char dir[PATH_MAX], path[PATH_MAX], tid[10];
    uint32_t next = 0xf001;
    snprintf(dir, sizeof(dir), "%s/user/bgft/task", p->root);
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (strlen(e->d_name) != 8)
            continue;
        uint32_t id = (uint32_t)strtoul(e->d_name, NULL, 16);
        snprintf(path, sizeof(path), "%s/%s/d0.pdb", dir, e->d_name);
        task_title(path, tid);
        if (!strcmp(tid, p->title_id)) {
            closedir(d);
            p->log("BGFT task %08x already exists for %s", id, p->title_id);
            return id;
        }
        if (id >= next)
            next = id + 1;
    }
    if (d)
        closedir(d);

    pkg_t pkg;
    const char *open_path = p->pkg_open_path ? p->pkg_open_path : p->pkg_path;
    if (pkg_open(&pkg, open_path)) {
        p->log("bgft: cannot open %s", open_path);
        return 0;
    }
    size_t pl = 0;
    uint8_t *pg = pkg_entry(&pkg, 0x1001, &pl);
    buf_t json = {0}, rec = {0};
    build_app_json(&json, &pkg, p->pkg_path);
    uint64_t ticks = (uint64_t)(unix_time + 62135596800ll) * 1000000ull; /* us since 0001-01-01 */
    const char *base = strrchr(p->pkg_path, '/');
    bgft_task_t t = {.task_id = next, .fw_raw = fw_raw, .created = ticks, .updated = ticks,
                     .src_url = p->pkg_path, .name = base ? base + 1 : p->pkg_path};
    uint32_t rc = 0;
    if (build_bgft_task(&rec, &pkg, pg, pl, &json, &t) == 0) {
        char rel[96];
        snprintf(path, sizeof(path), "%s/%08x", dir, next);
        mkpath(path, 0770);
        snprintf(rel, sizeof(rel), "/user/bgft/task/%08x/d0.pdb", next);
        int bad = put(p, rel, rec.data, rec.len, 0770);
        snprintf(rel, sizeof(rel), "/user/bgft/task/%08x/d1.pdb", next);
        bad |= put(p, rel, rec.data, rec.len, 0770);
        rc = bad ? 0 : next;
    } else {
        p->log("bgft: playgo-chunk.dat unusable");
    }
    free(pg);
    buf_free(&json);
    buf_free(&rec);
    pkg_close(&pkg);
    return rc;
}

static void rm_dir_files(place_t *p, const char *dir_rel, const char *const *names) {
    char path[PATH_MAX];
    for (int i = 0; names[i]; i++) {
        snprintf(path, sizeof(path), "%s%s/%s", p->root, dir_rel, names[i]);
        unlink(path);
    }
    snprintf(path, sizeof(path), "%s%s", p->root, dir_rel);
    rmdir(path);
}

void place_remove(place_t *p) {
    char d[128];
    static const char *const app[] = {"app.pkg", "app.json", "app.pbm", "app.pbm.backup", "app.xml", NULL};
    static const char *const playgo[] = {"pfs.pbm", "pfs.pbm.backup", NULL};
    static const char *const priv[] = {"param.sfo", "nptitle.dat", "npbind.dat", "ext_info.dat", NULL};
    static const char *const meta[] = {"icon0.png", "pic0.png", "pic1.png", "snd0.at9", "icon0.dds",
                                       "pic0.dds", "pic1.dds", "pronunciation.xml", "shareparam.json", NULL};
    snprintf(d, sizeof(d), "/user/app/%s", p->title_id);
    rm_dir_files(p, d, app);
    snprintf(d, sizeof(d), "/system_data/playgo/%s", p->title_id);
    rm_dir_files(p, d, playgo);
    snprintf(d, sizeof(d), "/system_data/priv/appmeta/%s", p->title_id);
    rm_dir_files(p, d, priv);
    snprintf(d, sizeof(d), "/user/appmeta/%s", p->title_id);
    rm_dir_files(p, d, meta);
}
