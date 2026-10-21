#include <stdarg.h>
#include <stdio.h>
#include "place.h"
static void lg(const char *f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); puts(""); }
int main(int c, char **v) { (void)c; place_t p = {.root = v[1], .pkg_path = v[2], .pkg_open_path = v[3], .log = lg}; if (place_read_ids(&p)) return 1; printf("task %x\n", place_bgft_task(&p, 0x12008011, 1761000000)); printf("again %x\n", place_bgft_task(&p, 0x12008011, 1761000000)); return 0; }
