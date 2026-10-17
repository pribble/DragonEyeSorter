// config.hpp — des_app configuration (src/des_app.json). Every calibration value lives here, not in code.
#pragma once

#include <string>
#include <vector>

#include "arm.hpp"
#include "flap.hpp"
#include "geom.hpp"
#include "measure_config.hpp"
#include "picker.hpp"
#include "tracker.hpp"

namespace des {

struct CameraConfig {
    int vmax = 0;                // IMX415 VMAX override after init (0 = keep the driver's 2283); 2276 -> 60.0 fps
    int buffers = 4;             // DMA ring; a frame stays valid for buffers-2 further frames
    bool realtime = true;        // SCHED_FIFO capture loop (PREEMPT_RT kernel)
};

struct LocateConfig {
    int scale = 4;               // work on a 1/scale gray image (4 -> 480x270, ~0.6 mm/px; 6.8 ms per frame on the board)
    int thresh = 0;              // fixed threshold; 0 = Otsu
    int min_thresh = 60;         // Otsu result is clamped up to this (empty dark belt must stay empty)
    int open_iter = 1;           // morphological opening passes (remove specks)
    int border_px = 2;           // blob within this many (small-image) px of the edge = partial
    float min_area_mm2 = 40;     // smaller blobs are dirt
    float max_area_mm2 = 3000;   // larger blobs are several touching parts (merged)
};

struct BeltConfig {
    double mm_per_step = 0.0207; // ruler, 10-16: 4 turns (12800 steps) = ~265 mm (effective roller ~21 mm); des_tool calib-belt
    int dir = 1;                 // +1: stepper POS increasing moves parts toward +x
    double run_speed = 50;       // mm/s
    double accel = 400;          // mm/s^2
};

// NPU part classifier (npu_cls.hpp / train/export_npu.py)
struct NpuConfig {
    bool enabled = true;
    std::string model = "part_cls.json";   // model JSON, relative to des_app.json; missing file = no NPU
    int core = 1;                          // CPU core of the NPU thread (it mostly waits for the NPU)
};

// local storage of inspection results (part_log.hpp)
struct StorageConfig {
    bool enabled = true;
    std::string dir = "data/log";   // 相对值按 des_app.json 所在目录解析（绝对值原样）→ 测试目录部署时各写各的
    std::string images = "ng";   // none | ng (flap + recheck) | all
    int jpeg_quality = 92;
    int max_queue = 256;
};

struct AppConfig {
    CameraConfig camera;
    LocateConfig locate;
    Homography px_to_table;      // image px -> table mm ⚠ calibrate (des_tool calib-cam)
    BeltConfig belt;
    TrackerConfig tracker;
    PickerConfig picker;
    ArmConfig arm;
    TrackMeasureConfig measure;
    StorageConfig storage;
    NpuConfig npu;
    FlapConfig flaps;            // belt-end flaps (des_pwm, v7)
    std::string label_list = "models/label_list.json";
    std::string calib_file = "calib.json";     // written by des_tool calib-cam, merged over this file if present
    std::string calib_loaded;                  // path actually merged (empty = none)
    float pick_after_fov = 0;                  // > 0: picker.pick_x = calibrated fov_x1 + this (mm)
    bool belt_calibrated = false;
    bool arm_home_taught = false;              // arm_home.json (HMI arm page) replaced arm.home_deg
    bool arm_calibrated = false;               // calib file had an "arm" section (des_tool calib-arm)
    std::string config_dir = ".";             // folder of the loaded des_app.json (ui_params.json lives there)
    std::vector<std::string> bins = {"gear_box", "rack_box"};          // pick destinations (arm drop poses)
    std::vector<std::string> outlets = {"recheck", "scrap", "rework"};  // flap outlets, index 0 = recheck
    // product recipes (配方, recipes.json next to des_app.json): the active one sets the spec in tracker.size.
    // Without the file there is one recipe (id 1) = measure.rule of des_app.json.
    std::string recipes_file = "recipes.json";
    std::vector<Recipe> recipes;
    int recipe = 1;                            // active recipe id at start (the HMI's ui_params.json may override)
};

// Loads path (missing keys keep their defaults) and builds tracker.rules from the label list.
// Returns an empty string on success, else an error message.
std::string load_config(const std::string &path, AppConfig &cfg);

}  // namespace des
