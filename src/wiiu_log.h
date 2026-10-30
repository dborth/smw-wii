// Logging for the Wii U glue code. Free of wut and upstream headers, so every file in src/ can include it.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Remembers a line in memory and sends it to OSReport (src/wiiu_crashlog.cpp). Nothing is written to the SD card
// here: the lines only reach <save dir>smw-log.txt if the game ends abnormally, so logging costs nothing while
// someone is playing.
void smw_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif
