// util.cpp — see util.hpp: the wall-clock state and the cold helpers.
#include "util.hpp"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <sys/stat.h>

#include <atomic>
#include <cstdarg>

namespace des {

static std::atomic<int64_t> g_wall_offset_ms{0};
static std::atomic<bool> g_wall_synced{false};

static int64_t realtime_ms()
{
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return int64_t(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

// part_log.cpp still defines the wall clock with strong symbols: weak keeps des_app / des_tool linking until the area
// agents delete theirs (then these take over); no call-site change needed.
#define DES_UTIL_WEAK __attribute__((weak))

DES_UTIL_WEAK void set_wall_offset_ms(int64_t offset_ms)
{
    g_wall_offset_ms = offset_ms;
    g_wall_synced = true;
}

DES_UTIL_WEAK int64_t wall_now_ms() { return realtime_ms() + g_wall_offset_ms.load(); }

DES_UTIL_WEAK bool wall_synced() { return g_wall_synced; }

void wall_apply_ms(int64_t plc_unix_ms, const char *src)
{
    (void)src;   // no stdout here: the runner keeps the HMI event text
    g_wall_offset_ms = plc_unix_ms - realtime_ms();
    g_wall_synced = true;
}

std::string fmt(const char *format, ...)
{
    char stack[512];
    va_list ap;
    va_start(ap, format);
    const int n = vsnprintf(stack, sizeof stack, format, ap);
    va_end(ap);
    if (n < 0) return std::string();
    if (size_t(n) < sizeof stack) return std::string(stack, size_t(n));
    std::string out(size_t(n) + 1, '\0');
    va_start(ap, format);
    vsnprintf(&out[0], out.size(), format, ap);
    va_end(ap);
    out.resize(size_t(n));
    return out;
}

DES_UTIL_WEAK bool make_dirs(const std::string &path)
{
    for (size_t i = 1; i <= path.size(); i++)
        if (i == path.size() || path[i] == '/') {
            std::string p = path.substr(0, i);
            if (mkdir(p.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
    return true;
}

std::string slurp(const std::string &path, bool *ok)
{
    if (ok) *ok = false;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return std::string();
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    const bool good = ferror(f) == 0;
    fclose(f);
    if (ok) *ok = good;
    return good ? s : std::string();
}

std::string dir_of(const std::string &path)
{
    const size_t k = path.find_last_of('/');
    return k == std::string::npos ? "." : path.substr(0, k);
}

void csv_sanitize(const char *in, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    size_t i = 0;
    if (in)
        for (; in[i] && i + 1 < cap; i++) out[i] = (in[i] == ',' || in[i] == '\n' || in[i] == '\r') ? ';' : in[i];
    out[i] = '\0';
}

int pin_to_core(int core)
{
    if (core < 0) return -1;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    const int r = pthread_setaffinity_np(pthread_self(), sizeof set, &set);
    if (r != 0) errno = r;   // pthread_* returns the error number instead of setting errno
    return r != 0 ? -1 : 0;
}

#undef DES_UTIL_WEAK
}  // namespace des
