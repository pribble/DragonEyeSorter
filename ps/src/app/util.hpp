// util.hpp — shared app-layer helpers: clocks, files, strings, CPU pinning. No OpenCV.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <time.h>

namespace des {

// CLOCK_MONOTONIC: seconds as a double / microseconds as uint64_t.
inline double mono_s()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
inline uint64_t mono_us()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000u + uint64_t(ts.tv_nsec) / 1000u;
}

// Wall clock = CLOCK_REALTIME + PLC offset (no RTC on the board); thread-safe, 0 offset until a sync arrives.
int64_t wall_now_ms();
bool wall_synced();
void set_wall_offset_ms(int64_t offset_ms);
void wall_apply_ms(int64_t plc_unix_ms, const char *src);   // offset = plc_ms - realtime, no stdout

// printf into a std::string (512-byte stack buffer, heap above that).
std::string fmt(const char *format, ...) __attribute__((format(printf, 1, 2)));

bool make_dirs(const std::string &path);                        // mkdir -p, 0755; false except EEXIST
std::string slurp(const std::string &path, bool *ok = nullptr); // whole file
std::string dir_of(const std::string &path);                    // "." when the path has no '/'
void csv_sanitize(const char *in, char *out, size_t cap);       // one CSV field: , \n \r -> ';'
int pin_to_core(int core);                                      // 0 = ok, -1 = failed (errno)

}  // namespace des
