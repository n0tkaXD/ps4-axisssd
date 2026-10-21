/* ifusb_attr: can a file on the exFAT drive be made unwritable for root?
 * Never truncates or writes data: write access is probed with open(O_WRONLY)
 * and closed at once. No mounts. Logs to klog with [ifusb]. */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ps4/kernel.h>
#include <ps4/klog.h>

#ifndef IFUSB_FILE
#error "build with -DIFUSB_FILE=..."
#endif

static void out(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    klog_printf("[ifusb] %s\n", buf);
}

static void show(const char *step, const char *p) {
    struct stat st;
    if (stat(p, &st))
        out("%s: stat: %s", step, strerror(errno));
    else
        out("%s: mode=%o flags=0x%x size=%lld", step, st.st_mode & 07777, st.st_flags, (long long)st.st_size);
}

static int writable(const char *step, const char *p) {
    int fd = open(p, O_WRONLY); /* no O_TRUNC: changes nothing */
    if (fd >= 0) {
        close(fd);
        out("%s: open(O_WRONLY) ALLOWED", step);
        return 1;
    }
    out("%s: open(O_WRONLY) refused: %s (errno %d)", step, strerror(errno), errno);
    return 0;
}

int main(void) {
    const char *p = IFUSB_FILE;
    pid_t pid = getpid();
    out("ifusb_attr v1: %s, uid=%d authid=0x%llx", p, getuid(), (unsigned long long)kernel_get_ucred_authid(pid));

    show("before", p);
    writable("before", p);

    if (chmod(p, 0444))
        out("chmod 0444: %s", strerror(errno));
    show("after chmod 0444", p);
    if (!writable("after chmod 0444", p)) {
        out("RESULT: read-only attribute protects the file");
        out("done");
        return 0;
    }

    if (chflags(p, UF_IMMUTABLE))
        out("chflags UF_IMMUTABLE: %s", strerror(errno));
    show("after UF_IMMUTABLE", p);
    if (!writable("after UF_IMMUTABLE", p)) {
        out("RESULT: UF_IMMUTABLE protects the file");
        out("done");
        return 0;
    }

    if (chflags(p, SF_IMMUTABLE))
        out("chflags SF_IMMUTABLE: %s", strerror(errno));
    show("after SF_IMMUTABLE", p);
    if (!writable("after SF_IMMUTABLE", p)) {
        out("RESULT: SF_IMMUTABLE protects the file");
        out("done");
        return 0;
    }
    out("RESULT: no attribute stops root from opening the file for write");
    out("done");
    return 0;
}
