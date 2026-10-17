// cli_common.cpp — see cli_common.hpp
#include "cli_common.hpp"

#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "camera_rig.hpp"
#include "config.hpp"

namespace des {

volatile sig_atomic_t g_stop = 0;
void on_sigint(int) { g_stop = 1; }

double now_s()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

bool parse(int argc, char **argv, Args &a)
{
    for (int i = 1; i < argc; i++) {
        std::string k = argv[i];
        auto val = [&](void) -> const char * { return i + 1 < argc ? argv[++i] : nullptr; };
        const char *v = nullptr;
        if (k == "--no-rt") a.rt = false;
        else if (k == "--exhaustive") a.exhaustive = true;
        else if (k == "--go") a.go = true;
        else if (k == "--can" && (v = val())) a.can_if = v;
        else if (k == "-c" && (v = val())) a.config = v;
        else if (k == "-n" && (v = val())) a.n = atof(v);
        else if (k == "--vmax" && (v = val())) a.vmax = atoi(v);
        else if (k == "--exp" && (v = val())) a.exp_us = atoi(v);
        else if (k == "--gain" && (v = val())) a.gain = atoi(v);
        else if (k == "--dump" && (v = val())) a.dump = v;
        else if (k == "--belt" && (v = val())) a.belt = atof(v);
        else if (k == "--board" && (v = val())) a.board = v;
        else if (k == "--square" && (v = val())) a.square = atof(v);
        else if (k == "--travel" && (v = val())) a.travel = v;
        else if (k == "--image" && (v = val())) a.image = v;
        else if (k == "--out" && (v = val())) a.out = v;
        else if (k == "--cls" && (v = val())) a.cls = v;
        else if (k == "--every" && (v = val())) a.every = atoi(v);
        else if (k == "--max" && (v = val())) a.maxpp = atoi(v);
        else if (k == "--full" && (v = val())) a.full = atoi(v);
        else if (k == "--steps" && (v = val())) a.steps = atoi(v);
        else if (k == "--marker-h" && (v = val())) a.marker_h = float(atof(v));
        else if (a.mode.empty() && k[0] != '-') a.mode = k;   // first plain word is the mode
        else if (a.sub.empty() && isalpha((unsigned char)k[0])) a.sub = k;   // then an optional sub-command
        else if (k[0] != '-' || (k.size() > 1 && (isdigit(k[1]) || k[1] == '.'))) a.pos.push_back(atof(k.c_str()));
        else return false;
    }
    return !a.mode.empty();
}

void usage_app()
{
    fprintf(stderr,
            "usage: des_app run|live [options]\n"
            "  run   [-n sec] [--can can0] [--go] [--belt mm/s]   production: line state machine, belt, arm, CAN PLC\n"
            "  live  [-n sec] [--belt mm/s] [--dump x.png]        demo: 60 fps HDMI + locate / track / measure, no belt / arm / CAN\n"
            "  common: -c cfg.json  -n N  --vmax N  --exp us  --gain dB*10  --can if  --dump file.png  --belt mm/s  --go  --no-rt\n"
            "  --version  print the build date/time\n"
            "operator / calibration / measurement modes are in des_tool (see ps/README.md)\n");
}

bool print_version_if_requested(int argc, char **argv, const char *prog)
{
    for (int i = 1; i < argc; i++)
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("%s build %s %s\n", prog, __DATE__, __TIME__);
            std::fflush(stdout);
            return true;
        }
    return false;
}

void go_realtime()
{
    // PREEMPT_RT kernel: a SCHED_FIFO capture loop re-arms the DMA within tens of microseconds
    sched_param sp{};
    sp.sched_priority = 60;
    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) perror("SCHED_FIFO");
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) perror("mlockall");
}

int open_camera(CameraRig &cam, const AppConfig &cfg, const Args &a)
{
    int vmax = a.vmax >= 0 ? a.vmax : cfg.camera.vmax;
    int exp = a.exp_us >= 0 ? a.exp_us : cfg.camera.exposure_us;
    int gain = a.gain >= 0 ? a.gain : cfg.camera.gain_db10;
    std::string err = cam.open(cfg.camera.buffers, vmax, exp, gain);
    if (!err.empty()) {
        fprintf(stderr, "camera: %s\n", err.c_str());
        return -1;
    }
    if (exp > 0) printf("camera open: VMAX %d, %d buffers, exposure %d us\n", cam.sensor_vmax(), cfg.camera.buffers, exp);
    else printf("camera open: VMAX %d, %d buffers, exposure driver default (~1 frame)\n", cam.sensor_vmax(),
                cfg.camera.buffers);
    return 0;
}

}  // namespace des
