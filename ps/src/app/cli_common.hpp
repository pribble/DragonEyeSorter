// cli_common.hpp — command-line plumbing shared by des_app (product runtime: run / live) and des_tool
// (operator tools: probe / locate / track / display / collect / measure / calib-* / arm / ik).
//
// Both binaries parse the same option set and need the same small helpers (SIGINT flag, monotonic clock,
// SCHED_FIFO + mlockall, camera open). Keeping them here means an option is added once, and the two entry
// points differ only in which modes they accept (see ps/README.md "四类产物").
#pragma once

#include <csignal>
#include <string>
#include <vector>

namespace des {

struct AppConfig;
class CameraRig;

// One flat option set for both binaries; each mode reads only the fields it documents (usage_app() here,
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
    std::vector<double> pos;
    std::string cls;           // collect: class label
    int every = 6, maxpp = 10, full = 0;
    int steps = 0;                // calib-belt: long move (0 = to ~75 % of the view)
    float marker_h = -1;          // calib-arm: height of the marker top above the belt (mm); < 0 = keep belt_z
};

extern volatile sig_atomic_t g_stop;   // set by on_sigint; every mode's loop polls it
void on_sigint(int sig);

double now_s();                                     // CLOCK_MONOTONIC in seconds

// false = not a valid command line (no mode / unknown option); caller prints its usage().
bool parse(int argc, char **argv, Args &a);

void usage_app();                                   // des_app: run / live only (des_tool's list lives in tool_main.cpp,
                                                    // so the product binary does not carry the operator command text)

// --version prints "<prog> build <__DATE__> <__TIME__>" and returns true (call before parse: no mode needed).
bool print_version_if_requested(int argc, char **argv, const char *prog);

void go_realtime();                                 // SCHED_FIFO 60 + mlockall (PREEMPT_RT kernel)
int open_camera(CameraRig &cam, const AppConfig &cfg, const Args &a);   // 0 = ok, else 1 (message printed)

}  // namespace des
