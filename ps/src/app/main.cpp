// main.cpp — des_app: the product runtime entry point (run / live only).
//
//   des_app run  [-n sec] [--can can0] [--go] [--belt mm/s]   production: live + belt (BeltDriver) + arm (ArmExec on
//                                                             its own thread) + the CAN "PLC" state machine (Runner)
//   des_app live [-n sec] [--belt mm/s] [--dump x.png]        60 fps HDMI with locate / tracking / measuring overlays,
//                                                             no belt / arm / CAN (demo / degraded mode)   --version                                                 build date/time
//
// Operator modes (probe / locate / track / display / collect / measure / calib-* / arm / ik) live in tool_main.cpp -> the des_tool binary. Keep this file to the runtime the autostart service runs: product logic
// must not be able to move the arm or the belt through a debug command (see ps/README.md "四类产物").
#include <cstdio>
#include <cstdlib>
#include <string>
#include "cli_common.hpp"
#include "config.hpp"
#include "des_hw.h"
#include "live.hpp"
using namespace des;
// des_app's usage text lives here (not in cli_common.cpp) so that the product binary does not carry the operator command list.
static void usage_app()
{
    fprintf(stderr, "usage: des_app run|live [options]\n" "  run   [-n sec] [--can can0] [--go] [--belt mm/s]   production: line state machine, belt, arm, CAN PLC\n" "  live  [-n sec] [--belt mm/s] [--dump x.png]        demo: 60 fps HDMI + locate / track / measure, no belt / arm / CAN\n"
            "  common: -c cfg.json  -n N  --vmax N  --exp us  --gain dB*10  --can if  --dump file.png  --belt mm/s  --go  --no-rt\n"
            "  --version  print the build date/time\n"
            "operator / calibration / measurement modes are in des_tool (see ps/README.md)\n");
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
        fprintf(stderr, "des_app: '%s' is an operator mode - use des_tool %s (see ps/README.md)\n", a.mode.c_str(), a.mode.c_str());
        usage_app();
        return 1;
    }
    AppConfig cfg;
    std::string err = load_config(a.config, cfg);
    if (!err.empty()) fprintf(stderr, "config: %s (using defaults)\n", err.c_str());
    print_config_summary(cfg, true);
    des_hw_t hw;
    if (app_bootstrap(hw, cfg, a)) return 1;
    int rc = 1;
    CameraSession cam(&hw, cfg, a);
    if (cam.ok()) {
        // run: the same live loop with the belt / arm / CAN extras attached (Runner drives them in the worker)
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
        rc = run_live(hw, cfg, cam.rig(), opt, g_stop);
    }
    des_hw_close(&hw);
    return rc;
}
