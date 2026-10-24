// cli_common.hpp — command-line plumbing shared by des_app (product runtime: run / live) and des_tool (operator tools: probe / locate / track / display / collect / measure / calib-* / arm / ik).
//
// Both binaries parse the same option set and need the same small helpers (SIGINT flag, monotonic clock, SCHED_FIFO +
// mlockall, camera open) and the same entry bootstrap (config summary, PL open, signals, realtime). Keeping them here
// means an option or a banner line is added once, and the two entry points differ only in which modes they accept (see ps/README.md "四类产物").
#pragma once
#include <array>
#include <csignal>
#include <cstdint>
#include <string>
#include "camera_rig.hpp"
#include "des_hw.h"
#include "util.hpp"
namespace des {
struct AppConfig;
// One flat option set for both binaries; each mode reads only the fields it documents (usage_app() in main.cpp /
// usage_tool() in tool_main.cpp — kept apart so the product binary does not carry the operator command list).
struct Args {
    std::string mode, config, dump;   // -c 缺省在 main() 里给：$DES_CONFIG，否则 ./des_app.json（见 des.env）
    std::string board = "9x6", travel = "right", image, out;
    double square = 0;
    bool exhaustive = false;
    std::string sub;       // second plain word (arm sub-command)
    bool go = false;       // arm: really move (default: print the frames only)
    std::string can_if;    // run: CAN interface for the PLC link
    double n = 0;
    int vmax = -1, exp_us = -1, gain = -1;
    bool rt = true;
    double belt = 0;
    std::array<float, 5> pos{};   // plain numbers after the sub-command (goto / pick / ik); extras are ignored
    uint8_t npos = 0;             // valid entries in pos (0..5)
    std::string cls;           // collect: class label
    int every = 6, maxpp = 10, full = 0;
    int steps = 0;                // calib-belt: long move (0 = to ~75 % of the view)
    float marker_h = -1;          // calib-arm: height of the marker top above the belt (mm); < 0 = keep belt_z
    float carry_mm = 0;           // calib-arm: belt carries each located marker this far downstream to the arm
    float part_od = 0;            // calib-beam: caliper OD of the gear (0 = from the image)
};
extern volatile sig_atomic_t g_stop;   // set by on_sigint; every mode's loop polls it
void on_sigint(int sig);
inline double now_s() { return mono_s(); }          // CLOCK_MONOTONIC in seconds (util.hpp)
// false = not a valid command line (no mode / unknown option); caller prints its usage().
bool parse(int argc, char **argv, Args &a);
// --version prints "<prog> build <__DATE__> <__TIME__>" and returns true (call before parse: no mode needed).
bool print_version_if_requested(int argc, char **argv, const char *prog);
void go_realtime();                                 // SCHED_FIFO 60 + mlockall (PREEMPT_RT kernel)
int open_camera(CameraRig &cam, const AppConfig &cfg, const Args &a);   // 0 = ok, else 1 (message printed)
// Camera open + the `camera open: VMAX ...` banner, in one RAII object; ok() = 0 / failed (message already printed).
class CameraSession {
public:
    CameraSession(des_hw_t *hw, const AppConfig &cfg, const Args &a) : cam_(hw), ok_(open_camera(cam_, cfg, a) == 0) {}
    bool ok() const { return ok_; }
    CameraRig &rig() { return cam_; }
    CameraRig *operator->() { return &cam_; }
    CameraRig &operator*() { return cam_; }
private:
    CameraRig cam_;
    bool ok_ = false;
};
// The three `config: ...` lines load_config's result prints (missing_calib_line = false for calib-cam, which does not need the camera mapping).
void print_config_summary(const AppConfig &cfg, bool missing_calib_line);
void install_stop_signals();   // SIGINT / SIGTERM -> g_stop (every mode's loop polls it)
// des_hw_open + `PL des_regs VERSION` + install_stop_signals + go_realtime when enabled; 0 = ok, else 1 (printed).
int app_bootstrap(des_hw_t &hw, const AppConfig &cfg, const Args &a);
}  // namespace des
