/* applog.h - a small log file (sd:/flashcube/flashcube.log) for problems
 * worth knowing about later. Also serves the music code's http_log_note. */
#pragma once
void app_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define http_log_note app_log
