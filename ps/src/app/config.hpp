// config.hpp — des_app configuration (src/des_app.json). Every calibration value lives here, not in code.
#pragma once
#include <string>
#include <vector>
#include "arm.hpp"
#include "geom.hpp"
#include "measure_config.hpp"
#include "picker.hpp"
#include "tracker.hpp"
namespace des {
struct CameraConfig {
    int vmax = 0;                // IMX415 VMAX override after init (0 = keep the driver's 2283); 2276 -> 60.0 fps
    int buffers = 4;             // DMA ring; a frame stays valid for buffers-2 further frames
    bool realtime = true;        // SCHED_FIFO capture loop (PREEMPT_RT kernel)
    int exposure_us = 0;         // 0 = driver default (SHR0 8: almost the whole frame, ~16.6 ms); --exp overrides
    int gain_db10 = 0;           // sensor gain in 0.1 dB (0 = driver default); --gain overrides
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
    SpeedAdaptConfig adapt;      // slow down instead of stopping while the arm is busy (sched/picker.hpp)
    // measuring-wheel encoder on the belt (des_io, v11): E6B2-CWZ6C 1000 P/R decoded x4, 200 mm wheel (nominal);
    // des_tool calib-enc measures the real scale against the stepper (calib.json "encoder")
    int enc_counts_per_rev = 4000;
    double enc_wheel_mm = 200;
    double enc_mm_per_count = 0;  // 0 = not calibrated: enc_wheel_mm / enc_counts_per_rev
    int enc_dir = 1;              // +1: the count increases while parts move downstream (+x)
    double enc_scale() const { return enc_mm_per_count > 0 ? enc_mm_per_count : enc_wheel_mm / enc_counts_per_rev; }
};
// NPU part classifier (app/npu.hpp / train/export_npu.py)
struct NpuConfig {
    bool enabled = true;
    std::string model = "part_cls.json";   // model JSON, relative to des_app.json; missing file = no NPU
    int core = 1;                          // CPU core of the NPU thread (it mostly waits for the NPU)
};
// local storage of inspection results (app/part_log.hpp)
struct StorageConfig {
    bool enabled = true;
    std::string dir = "data/log";   // 相对值按 des_app.json 所在目录解析（绝对值原样）→ 测试目录部署时各写各的
    std::string images = "ng";   // none | ng (flap + recheck) | all
    int jpeg_quality = 92;
    int max_queue = 256;
    int feed_port = 0;           // > 0: live upload of records + images to a PC client (app/part_log)
};
// line PLC over Ethernet (hw/modbus_tcp.hpp: register map + Modbus-TCP slave); CAN is chosen with `run --can`
struct PlcConfig {
    int modbus_port = 0;         // > 0: Modbus-TCP slave on this port in `des_app run` (502 = standard)
};
struct AppConfig {
    CameraConfig camera;
    LocateConfig locate;
    Homography px_to_table;      // image px -> table mm on the belt plane ⚠ calibrate (des_tool calib-cam)
    Homography px_to_part;       // image px -> where a part stands (parallax of its height; = px_to_table while
                                 // measure.camera_h_mm = 0): locate / track / pick / overlay / crops (load_config)
    BeltConfig belt;
    TrackerConfig tracker;
    PickerConfig picker;
    ArmConfig arm;
    TrackMeasureConfig measure;
    StorageConfig storage;
    NpuConfig npu;
    FlapConfig flaps;            // side diverter gates (des_pwm, v7; in picker.hpp)
    PlcConfig plc;
    std::string label_list = "models/label_list.json";
    std::string calib_file = "calib.json";     // written by des_tool calib-cam, merged over this file if present
    std::string calib_loaded;                  // path actually merged (empty = none)
    std::string flat_file = "flat.json";       // des_tool calib-flat: white level of the view for the crops ("" = off)
    std::string flat_loaded;                   // path actually loaded (empty = none, crops raw)
    float pick_after_fov = 0;                  // > 0: picker.pick_x = calibrated fov_x1 + this (mm)
    bool belt_calibrated = false;
    bool arm_home_taught = false;              // arm_home.json (HMI arm page) replaced arm.home_deg
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
