// Logging for the Wii glue code. Free of libogc and upstream headers, so every file in src/ can include it.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Remembers a line in memory (src/wii_crashlog.cpp). Nothing is written to the SD card here: the lines only
// reach <root>smw-log.txt if the game ends abnormally, so logging costs nothing while someone is playing.
void smw_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif
