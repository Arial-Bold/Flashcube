/* applog.c - appends lines to the log; starts afresh when it passes 64 KB. */
#include "applog.h"
#include "storage.h"
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>

void app_log(const char *fmt, ...) {
    if (!storage_ok()) return;
    char path[300];
    storage_path(path, sizeof(path), "flashcube.log");
    struct stat st;
    const char *mode = (stat(path, &st) == 0 && st.st_size > 64 * 1024) ? "wb" : "ab";
    FILE *f = fopen(path, mode);
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}
