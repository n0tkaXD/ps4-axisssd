#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "build.h"
#include "pkg.h"
int main(int c, char **v) {
    (void)c; pkg_t p; if (pkg_open(&p, v[1])) return 1;
    size_t sl, pl; uint8_t *s = pkg_entry(&p, 0x1000, &sl), *pg = pkg_entry(&p, 0x1001, &pl); sfo_t sfo; sfo_parse(s, sl, &sfo);
    const sfo_item_t *av = sfo_get(&sfo, "APP_VER");
    buf_t json = {0}, pdb = {0};
    build_app_json(&json, &p, v[2]);
    build_pdb(&pdb, &p, av ? av->sval : "01.00", v[2], &json, pg, pl, NULL);
    FILE *f = fopen(v[3], "wb"); fwrite(pdb.data, 1, pdb.len, f); fclose(f);
    printf("pdb %zu bytes, ver %s\n", pdb.len, av ? av->sval : "?");
    return 0;
}
