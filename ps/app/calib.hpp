// calib.hpp — camera -> table calibration from one image of a checkerboard lying flat on the belt.
//
// Table frame (see scheduler/geom.hpp): x along belt travel, y 90 deg counter-clockwise from x seen from above
// (right-handed, z up). Lay the board flat on the belt with one edge against the side guard: the board axis
// closest to the travel direction becomes x. Origin: x = 0 at the upstream image edge on the center row,
// y = 0 on the image center line, so pick line / belt end can be measured with a ruler from the FOV edge.
// Result: the px -> table homography of the belt plane (parts' top faces sit higher: parallax, see
// 齿轮齿条_测量功能与数据.md), the FOV extent, and the residual of the fit.
#pragma once

#include <string>
#include <vector>

#include <opencv2/core.hpp>

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
// Draw detected corners, table axes at the origin and a 50 mm grid onto a BGR image.
void draw_calib(cv::Mat &bgr, const CalibResult &r);

}  // namespace des
