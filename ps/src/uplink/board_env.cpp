// board_env.cpp — see board_env.hpp
#include "board_env.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <vector>

namespace des {
namespace {

// the newest regular file in any direct subfolder of dir: named exactly `name`, or (prefix) starting with `name` and
// ending in ".csv"
bool newest_file(const std::string &dir, const char *name, bool prefix, std::string &path, time_t &mtime)
{
    DIR *d = opendir(dir.c_str());
    if (!d) return false;
    bool found = false;
    auto consider = [&](const std::string &p) {
        struct stat st{};
        if (stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && (!found || st.st_mtime > mtime)) { path = p; mtime = st.st_mtime; found = true; }
    };
    const size_t nlen = strlen(name);
    while (const dirent *e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string sub = dir + "/" + e->d_name;
        struct stat st{};
        if (stat(sub.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        if (!prefix) { consider(sub + "/" + name); continue; }
        DIR *s = opendir(sub.c_str());
        if (!s) continue;
        while (const dirent *f = readdir(s)) {
            const std::string n = f->d_name;
            if (n.size() > nlen + 4 && n.compare(0, nlen, name) == 0 && n.compare(n.size() - 4, 4, ".csv") == 0) consider(sub + "/" + n);
        }
        closedir(s);
    }
    closedir(d);
    return found;
}

std::vector<std::string> split(const std::string &s)
{
    std::vector<std::string> v;
    size_t a = 0;
    for (;;) {
        const size_t b = s.find(',', a);
        v.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) return v;
        a = b + 1;
    }
}

}  // namespace

bool csv_last_row(const std::string &path, std::string &header, std::string &row)
{
    std::ifstream f(path, std::ios::binary);
    if (!f || !std::getline(f, header)) return false;
    if (!header.empty() && header.back() == '\r') header.pop_back();
    const std::streamoff body = f.tellg();
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (body < 0 || size <= body) return false;
    const std::streamoff from = std::max(body, size - 512);
    std::string tail(size_t(size - from), '\0');
    f.seekg(from);
    if (!f.read(&tail[0], std::streamsize(tail.size()))) return false;
    const size_t end = tail.rfind('\n');          // only complete lines: the writer may be half way through one
    if (end == std::string::npos) return false;
    const size_t begin = end == 0 ? std::string::npos : tail.rfind('\n', end - 1);
    if (begin == std::string::npos && from != body) return false;   // a single line longer than the window
    row = tail.substr(begin == std::string::npos ? 0 : begin + 1, end - (begin == std::string::npos ? 0 : begin + 1));
    if (!row.empty() && row.back() == '\r') row.pop_back();
    return !row.empty();
}

double csv_field(const std::string &header, const std::string &row, const char *column, bool &ok)
{
    ok = false;
    const std::vector<std::string> h = split(header), r = split(row);
    for (size_t i = 0; i < h.size() && i < r.size(); i++)
        if (h[i] == column) {
            char *e = nullptr;
            const double v = strtod(r[i].c_str(), &e);
            ok = e != r[i].c_str() && std::isfinite(v);
            return ok ? v : 0;
        }
    return 0;
}

BoardEnv BoardEnvReader::read()
{
    BoardEnv e;
    const time_t now = time(nullptr);
    std::string path, header, row;
    time_t mt = 0;
    if (!log_dir_.empty() && newest_file(log_dir_, "env.csv", false, path, mt) && csv_last_row(path, header, row)) {
        bool a = false, b = false;
        const double t = csv_field(header, row, "temp_c", a), h = csv_field(header, row, "hum_rh", b);
        if (a && b) { e.env = true; e.temp_c = t; e.hum_rh = h; e.env_age_s = std::max(0.0, double(now - mt)); }
    }
    if (!log_dir_.empty() && newest_file(log_dir_, "vib_", true, path, mt) && csv_last_row(path, header, row)) {
        bool a = false, b = false;
        const double r = csv_field(header, row, "rms_g", a), hz = csv_field(header, row, "peak_hz", b);
        if (a && b) { e.vib = true; e.vib_rms_g = r; e.vib_peak_hz = hz; e.vib_age_s = std::max(0.0, double(now - mt)); }
    }
    if (std::ifstream st(proc_ + "/stat"); st) {   // "cpu user nice system idle iowait irq softirq steal ..."
        std::string tag;
        uint64_t v = 0, total = 0, idle = 0;
        if (st >> tag && tag == "cpu") {
            for (int i = 0; i < 8 && st >> v; i++) { total += v; if (i == 3 || i == 4) idle += v; }
            const uint64_t busy = total - idle;
            if (cpu_total_ && total > cpu_total_ && busy >= cpu_busy_)
                e.cpu_pct = int(std::lround(100.0 * double(busy - cpu_busy_) / double(total - cpu_total_)));
            cpu_busy_ = busy; cpu_total_ = total;
        }
    }
    if (std::ifstream m(proc_ + "/meminfo"); m) {
        std::string key, unit;
        double val = 0, total = 0, avail = -1;
        while (m >> key >> val) {
            std::getline(m, unit);
            if (key == "MemTotal:") total = val;
            else if (key == "MemAvailable:") avail = val;
        }
        if (total > 0 && avail >= 0) e.mem_avail_pct = int(std::lround(100 * avail / total));
    }
    struct statvfs fs{};
    if (!log_dir_.empty() && statvfs(log_dir_.c_str(), &fs) == 0 && fs.f_blocks > 0) {
        e.disk_free_mb = double(fs.f_bavail) * fs.f_frsize / 1048576.0;
        e.disk_free_pct = int(std::lround(100.0 * double(fs.f_bavail) / double(fs.f_blocks)));
    }
    if (std::ifstream u(proc_ + "/uptime"); u) {
        double up = 0;
        if (u >> up) e.board_up_s = up;
    }
    return e;
}

}  // namespace des
