#include <stdarg.h>
#include "place.h"
static void lg(const char *f, ...) { (void)f; }
int main(int c, char **v) { (void)c; place_t p = {.root = v[1], .pkg_path = "/mnt/usb0/itemzflow/apps/EMSN00001/app.pkg", .pkg_open_path = v[2], .language_mask = place_language_mask(8), .log = lg}; return place_files(&p); }
