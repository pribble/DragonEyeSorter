// tool_util.hpp — des_tool-only helpers (operator bring-up / calibration). OPS_SRC in the Makefile: the product binary des_app does not link this (see ps/README.md "四类产物").
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <opencv2/core.hpp>
#include "camera_rig.hpp"
#include "cli_common.hpp"
#include "config.hpp"
#include "locate.hpp"
namespace des {
// Stepper rates for a belt speed: steps/s and steps/s^2.
struct BeltRates {
    double vmax = 0, acc = 0;
};
// mm_s table mm/s -> (vmax = mm_s / |belt.mm_per_step|, acc). accel_mm_s2 < 0: acc = 2 * vmax (0.5 s ramps, every calibration mode); otherwise accel_mm_s2 / |belt.mm_per_step| (run_track velocity mode / calib-enc).
BeltRates belt_rates(const AppConfig &cfg, double mm_s, double accel_mm_s2 = -1);
// Enable the stepper driver and give it 100 ms to apply (des_step_enable + sleep) before the first move.
void belt_prepare(des_hw_t &hw);
// Absolute move by `steps` with a wait. sample != nullptr also records (time, POS, encoder) every 10 ms like
// belt_move_sampled did: 5 samples before the move, one per poll while it moves, 30 after it stopped (calib-enc). "" on success, else why not.
std::string belt_move(des_hw_t &hw, int32_t steps, double vmax, double acc, const std::function<void()> *sample = nullptr);
// Camera warm-up: discard n frames (exposure / VMAX settling, or frames exposed during a move). The last one keeps its pixels when keep_last. "" on success, else the camera error.
std::string capture_frames(CameraRig &cam, int n, CapturedFrame &f, bool keep_last = false);
// 4 full frames averaged into an 8-bit gray view (flat-field white level). `sum` is scratch (CV_32F) reused across calls, so the calibration loop allocates it once instead of per stop.
std::string capture_gray_avg(CameraRig &cam, int discard, cv::Mat &sum, cv::Mat &gray);
// Still-pose position of the largest fully visible part, median over 7 frames (calib-belt / calib-arm). "" on success.
// h = nullptr: belt-plane mapping (px_to_table, flat paper); &cfg.px_to_part for a part of gear_h_mm (calib-beam).
// len != nullptr: median long side (round_fit: enclosing-circle diameter).
std::string still_position(CameraRig &cam, Locator &loc, const AppConfig &cfg, Vec2 &p, const Homography *h = nullptr,
                           float *len = nullptr);
// --image: BGR frame + its gray (both are needed: gray for detection, BGR for --dump / draw). false = unreadable.
bool read_image(const std::string &path, cv::Mat &bgr, cv::Mat &gray);
// Which default output file a calibration mode would update anyway: calib.json next to the config, the merged calib file, or flat.json / cfg.flat_file.
enum class CalibDefault : uint8_t { Camera, Belt, Encoder, Flat, Arm, Beam };
std::string calib_out(const AppConfig &cfg, const Args &a, CalibDefault which);
// `saved <path>: ok` / `saved <path>: <err>`; ok_extra is appended after "ok" (flat's "flat_file is empty" note).
void report_saved(const std::string &out, const std::string &err, const char *ok_extra = nullptr);
}  // namespace des
