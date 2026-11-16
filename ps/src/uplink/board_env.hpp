// board_env.hpp — environment and board health for the 4G state, read without asking des_app for anything:
//   - temperature / humidity: the last row of the newest env.csv that des_app writes into its session folder
//     (<log_dir>/<date>/env.csv, one row every 10 s while the RS485 transmitter answers)
//   - belt motor vibration: the last row of the newest vib_*.csv there (only when des_app has "vib" enabled)
//   - the board itself: CPU busy share (/proc/stat, between two reads), memory available (/proc/meminfo), free space
//     on the SD card (statvfs of log_dir), time since boot (/proc/uptime)
// The age of a reading is "now - the file's modification time" on the board clock (no RTC: both sides count from
// boot the same way), so a sensor that stopped answering shows up as an old reading, not as a wrong value.
#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace des {

struct BoardEnv {
    bool env = false;                 // temp_c / hum_rh hold a reading
    double temp_c = 0, hum_rh = 0, env_age_s = -1;
    bool vib = false;                 // vib_rms_g / vib_peak_hz hold a reading
    double vib_rms_g = 0, vib_peak_hz = 0, vib_age_s = -1;
    int cpu_pct = -1;                 // -1 = not known yet (needs two reads)
    int mem_avail_pct = -1;
    double disk_free_mb = -1;
    int disk_free_pct = -1;
    double board_up_s = -1;
};

class BoardEnvReader {
public:
    // log_dir: des_app's storage.dir (session folders below it); proc: "/proc" (tests point it at a folder of fakes)
    explicit BoardEnvReader(std::string log_dir, std::string proc = "/proc") : log_dir_(std::move(log_dir)), proc_(std::move(proc)) {}
    BoardEnv read();                  // one pass: a few small files, cheap enough every few seconds

private:
    std::string log_dir_, proc_;
    uint64_t cpu_busy_ = 0, cpu_total_ = 0;
};

// helpers, public for tests
bool csv_last_row(const std::string &path, std::string &header, std::string &row);   // false: no data row
double csv_field(const std::string &header, const std::string &row, const char *column, bool &ok);

}  // namespace des
