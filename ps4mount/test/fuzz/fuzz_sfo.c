/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Fuzz target: param.sfo parser (data from pkgs on USB drives). */
#include <stddef.h>
#include <stdint.h>

#include "pkg.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static sfo_t sfo;
    if (!sfo_parse(data, size, &sfo)) {
        sfo_get(&sfo, "TITLE");
        sfo_get(&sfo, "APP_VER");
        sfo_get(&sfo, "CONTENT_ID");
    }
    return 0;
}
