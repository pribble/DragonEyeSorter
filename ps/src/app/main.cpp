// main.cpp — des_app: the product runtime entry point (run / live only).
//
//   des_app run  [-n sec] [--can can0] [--go] [--belt mm/s]   production: live + belt (BeltDriver) + arm (ArmExec on
//                                                             its own thread) + the CAN "PLC" state machine (Runner)
//   des_app live [-n sec] [--belt mm/s] [--dump x.png]        60 fps HDMI with locate / tracking / measuring overlays,
//                                                             no belt / arm / CAN (demo / degraded mode)
//   --version                                                 build date/time
//
// Operator modes (probe / locate / track / display / collect / measure / calib-* / arm / ik) live in
// tool_main.cpp -> the des_tool binary. Keep this file to the runtime the autostart service runs: product logic
// must not be able to move the arm or the belt through a debug command (see ps/README.md "四类产物").
#include <signal.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "cli_common.hpp"
#include "camera_rig.hpp"
#include "config.hpp"
#include "des_hw.h"
#include "live.hpp"

using namespace des;

// live with the run extras attached: Runner drives the belt, the arm thread and the CAN PLC link in the worker.
static int run_product(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    CameraRig cam(&hw);
    if (open_camera(cam, cfg, a)) return 1;
    LiveOptions opt;
    opt.seconds = a.n;
    opt.belt_mm_s = a.belt;
    opt.dump = a.dump;
    if (a.mode == "run") {
        opt.run = true;
        opt.run_opt.can_if = a.can_if;
        opt.run_opt.arm_go = a.go;
        opt.run_opt.speed_mm_s = a.belt;
        opt.belt_mm_s = 0;
    }
    return run_live(hw, cfg, cam, opt, g_stop);
}

int main(int argc, char **argv)
{
    if (print_version_if_requested(argc, argv, "des_app")) return 0;

    Args a;
    if (!parse(argc, argv, a)) { usage_app(); return 1; }
    if (a.config.empty()) {   // 没给 -c：环境里的 DES_CONFIG（des.env export 的），否则当前目录的 des_app.json
        const char *e = getenv("DES_CONFIG");
        a.config = (e && *e) ? e : "des_app.json";
    }
    if (a.mode != "run" && a.mode != "live") {
        fprintf(stderr, "des_app: '%s' is an operator mode - use des_tool %s (see ps/README.md)\n", a.mode.c_str(),
                a.mode.c_str());
        usage_app();
        return 1;
    }

    AppConfig cfg;
    std::string err = load_config(a.config, cfg);
    if (!err.empty()) fprintf(stderr, "config: %s (using defaults)\n", err.c_str());
    if (!cfg.calib_loaded.empty()) printf("config: camera calibration from %s\n", cfg.calib_loaded.c_str());
    else printf("config: no %s, camera mapping is the uncalibrated placeholder\n", cfg.calib_file.c_str());

    des_hw_t hw;
    int rc = des_hw_open(&hw);
    if (rc) {
        fprintf(stderr, "PL: %s\n", des_strerror(rc));
        return 1;
    }
    printf("PL des_regs VERSION %u\n", hw.version);
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);
    if (a.rt && cfg.camera.realtime) go_realtime();

    rc = run_product(hw, cfg, a);
    des_hw_close(&hw);
    return rc;
}
