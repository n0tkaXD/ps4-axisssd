/* ifusb_chmod: sets IFUSB_MODE on IFUSB_FILE and reports whether a write open
 * (no O_TRUNC, changes nothing) is allowed afterwards. Test helper. */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ps4/klog.h>

#if !defined(IFUSB_FILE) || !defined(IFUSB_MODE)
#error "build with -DIFUSB_FILE=... -DIFUSB_MODE=..."
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
    struct stat st;
    if (chmod(IFUSB_FILE, IFUSB_MODE))
        out("chmod %o %s: %s", IFUSB_MODE, IFUSB_FILE, strerror(errno));
    if (!stat(IFUSB_FILE, &st))
        out("%s: mode now %o", IFUSB_FILE, st.st_mode & 07777);
    int fd = open(IFUSB_FILE, O_WRONLY);
    out("write open: %s", fd >= 0 ? "allowed" : strerror(errno));
    if (fd >= 0)
        close(fd);
    out("done");
    return 0;
}
