/* ifusb_swap: test helper, reversible.
 * IFUSB_UNDO not set: /user/app/<TID>/app.pkg (a normal install) is renamed to
 *   app.pkg.internal and replaced by a symlink to IFUSB_PKG on the drive.
 * IFUSB_UNDO set: the symlink is removed and app.pkg.internal renamed back. */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ps4/klog.h>

#if !defined(IFUSB_TID) || !defined(IFUSB_PKG)
#error "build with -DIFUSB_TID=... -DIFUSB_PKG=..."
#endif

static void out(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    klog_printf("[ifusb] %s\n", buf);
}

int main(void) {
    char pkg[128], keep[128];
    struct stat st;
    snprintf(pkg, sizeof(pkg), "/user/app/%s/app.pkg", IFUSB_TID);
    snprintf(keep, sizeof(keep), "/user/app/%s/app.pkg.internal", IFUSB_TID);
#ifndef IFUSB_UNDO
    out("ifusb_swap: %s -> %s", pkg, IFUSB_PKG);
    if (lstat(pkg, &st) || !S_ISREG(st.st_mode)) {
        out("ABORT: %s is not a regular file", pkg);
        return 1;
    }
    if (lstat(keep, &st) == 0) {
        out("ABORT: %s already exists", keep);
        return 1;
    }
    if (stat(IFUSB_PKG, &st) || !S_ISREG(st.st_mode)) {
        out("ABORT: %s not found", IFUSB_PKG);
        return 1;
    }
    if (rename(pkg, keep)) {
        out("ABORT: rename: %s", strerror(errno));
        return 1;
    }
    if (symlink(IFUSB_PKG, pkg)) {
        out("symlink failed (%s), putting the file back", strerror(errno));
        rename(keep, pkg);
        return 1;
    }
    out("swapped: %s is now a link, original kept as %s", pkg, keep);
#else
    out("ifusb_swap undo: %s", pkg);
    if (lstat(keep, &st) || !S_ISREG(st.st_mode)) {
        out("ABORT: %s missing", keep);
        return 1;
    }
    if (lstat(pkg, &st) == 0) {
        if (!S_ISLNK(st.st_mode)) {
            out("ABORT: %s is a regular file, not touching", pkg);
            return 1;
        }
        unlink(pkg);
    }
    if (rename(keep, pkg)) {
        out("ABORT: rename back: %s", strerror(errno));
        return 1;
    }
    out("restored %s", pkg);
#endif
    out("done");
    return 0;
}
