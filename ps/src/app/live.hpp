// live.hpp — the integrated 60 fps loop: camera -> HDMI with overlays (core 0) + locate / track / pick (core 1).
#pragma once

#include <csignal>
#include <string>

#include "camera_rig.hpp"
#include "collector.hpp"
#include "config.hpp"
#include "des_hw.h"
#include "runner.hpp"

namespace des {

struct LiveOptions {
    double seconds = 0;        // 0 = until Ctrl-C
    double belt_mm_s = 0;      // != 0: run the belt in velocity mode (bring-up); picker plans are only shown
    std::string dump;          // save the displayed image (PNG) once, 3 s in, for checking the overlay remotely
    bool run = false;          // production: belt + arm + CAN through Runner (else dry tracking only)
    RunOptions run_opt;
    CollectOptions collect;    // label set: also save training crops (des_tool collect)
};

// cam must already be open. Returns 0 on success.
int run_live(des_hw_t &hw, const AppConfig &cfg, CameraRig &cam, const LiveOptions &opt, const volatile sig_atomic_t &stop);

}  // namespace des
