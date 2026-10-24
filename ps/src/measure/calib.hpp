// calib.hpp — camera -> table calibration from one image of a checkerboard lying flat on the belt.
//
// Table frame (see sched/geom.hpp): x along belt travel, y 90 deg counter-clockwise from x seen from above
// (right-handed, z up). Lay the board flat on the belt with one edge against the side guard: the board axis
// closest to the travel direction becomes x. Origin: x = 0 at the upstream image edge on the center row,
// y = 0 on the image center line, so pick line / belt end can be measured with a ruler from the FOV edge.
// Result: the px -> table homography of the belt plane (parts' top faces sit higher: parallax, see
// 齿轮齿条_测量功能与数据.md), the FOV extent, and the residual of the fit.
#pragma once

#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "arm.hpp"
#include "geom.hpp"

namespace des {

struct BoardSpec {
    int cols = 9, rows = 6;      // inner corners
    float square_mm = 25;        // ⚠ measure the printed board with a caliper (printers scale)
};

enum class Travel { Right, Left, Down, Up };   // direction the belt moves in the image
bool parse_travel(const std::string &s, Travel &t);
const char *to_str(Travel t);

struct CalibResult {
    bool ok = false;
    std::string err;
    Homography px_to_table;
    float rms_mm = 0, max_mm = 0;        // fit residual at the corners
    float mm_per_px = 0;                 // at the image center
    float fov_x0 = 0, fov_x1 = 0;        // table x of the upstream / downstream image edge, center row
    Vec2 fov_corners[4];                 // table coords of image corners (0,0) (W,0) (W,H) (0,H)
    std::vector<cv::Point2f> corners;    // detected corners, image px
    double detect_ms = 0;
};

// Corner detection on a full-resolution 8-bit image: classic detector -> sector detector at half resolution ->
// (exhaustive) full-resolution sector search (~12 s on the A35). False if not found.
bool detect_board(const cv::Mat &gray, const BoardSpec &board, bool exhaustive, std::vector<cv::Point2f> &corners);
// Same, in a child process killed after timeout_s: OpenCV's detectors can search for minutes when the board in view
// has another corner count than --board. Returns 1 found, 0 not found, -1 timed out.
int detect_board_timeout(const cv::Mat &gray, const BoardSpec &board, bool exhaustive, double timeout_s,
                         std::vector<cv::Point2f> &corners);
// Homography, axes, origin and FOV from detected corners (row-major order as OpenCV returns them).
CalibResult calibrate_from_corners(const std::vector<cv::Point2f> &corners, cv::Size image, const BoardSpec &board,
                                   Travel travel);
// detect_board (in process) + calibrate_from_corners
CalibResult calibrate_table(const cv::Mat &gray, const BoardSpec &board, Travel travel, bool exhaustive = false);
// Writes px_to_table + tracker fov into a JSON merged by load_config (app/config.cpp). Empty string = ok.
std::string save_calib(const std::string &path, const CalibResult &r, const BoardSpec &board, Travel travel);
// Belt scale from one part seen by the calibrated camera (des_tool calib-belt): positions (table mm, belt still)
// p0 at the start, p1 after s1 steps, p2 after s1 + s2 steps, p3 after returning to the start.
struct BeltCalib {
    bool ok = false;
    std::string err;
    double mm_per_step = 0;      // > 0
    int dir = 1;                 // +1: stepper POS increasing moves parts toward +x (downstream)
    double travel_mm = 0;        // |p2 - p0| along x
    int32_t steps = 0;           // s1 + s2
    double return_err_mm = 0;    // p3.x - p0.x after moving back by the same steps (slip / backlash)
    double drift_mm_per_100 = 0; // lateral (y) change per 100 mm of travel: belt or camera not square
    double roller_d_mm = 0;      // effective drive roller diameter for steps_per_rev
};
BeltCalib belt_from_positions(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, int32_t s1, int32_t s2, int steps_per_rev = 3200);
// Adds "belt": {mm_per_step, dir, ...} to the calib JSON (keeps the camera part). Empty string = ok.
std::string save_belt_calib(const std::string &path, const BeltCalib &b);

// Belt encoder (measuring wheel riding on the belt, des_io x4 counts) against the stepper (des_tool calib-enc): the
// belt moves `steps` downstream and back while (time, stepper POS, encoder count) are sampled every ~10 ms. The
// stepper scale (mm_per_step, from calib-belt) gives the encoder its mm; the encoder then shows what the stepper
// cannot: slip / lag of the belt behind the roller and speed ripple.
struct EncSample {
    double t;      // s
    int32_t pos;   // stepper POS
    int32_t enc;   // encoder count
};
struct EncCalib {
    bool ok = false;
    std::string err;
    double counts_per_step = 0;     // forward move, |counts| / |steps|
    double back_counts_per_step = 0;
    double mm_per_count = 0;        // > 0
    int dir = 1;                    // +1: the count increases while parts move downstream (+x)
    double travel_mm = 0;
    double wheel_circ_mm = 0;       // counts_per_rev x mm_per_count (vs the wheel's nominal circumference)
    double return_err_mm = 0;       // encoder after forward + back by the same steps (belt creep / backlash)
    double cruise_mm_s = 0;         // stepper speed in the constant-speed part of the forward move
    double enc_speed_mean_mm_s = 0, enc_speed_sd_mm_s = 0;   // encoder speed there, 0.2 s windows
    double lag_max_mm = 0;          // max |encoder mm - stepper mm| during the forward move (ends fitted to 0)
};
// fwd / back: samples from just before each move starts until after it stopped. dir_steps: cfg belt dir (+1 = POS
// increasing moves parts downstream). nominal_mm_per_count: wheel circumference / counts per rev (sanity check).
EncCalib enc_from_samples(const std::vector<EncSample> &fwd, const std::vector<EncSample> &back, double mm_per_step,
                          int dir_steps, int counts_per_rev, double nominal_mm_per_count);
// Adds "encoder": {mm_per_count, dir, ...} to the calib JSON. Empty string = ok.
std::string save_enc_calib(const std::string &path, const EncCalib &e, int rounds, double mm_per_count_sd);
// Adds "arm": {base_xy, base_yaw_deg, belt_z (if with_belt_z), fit residuals} (des_tool calib-arm). Empty = ok.
std::string save_arm_calib(const std::string &path, const ArmBaseFit &f, bool with_belt_z);
// Adds "beam": {x_mm, sd_mm, rounds, part_r_mm, part_y_mm} (des_tool calib-beam: through-beam position). Empty = ok.
std::string save_beam_calib(const std::string &path, double x_mm, double sd_mm, int rounds, double part_r_mm, double y_mm);
// Draw detected corners, table axes at the origin and a 50 mm grid onto a BGR image.
void draw_calib(cv::Mat &bgr, const CalibResult &r);

}  // namespace des
