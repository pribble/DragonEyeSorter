// flat_field.hpp — white level over the view (4.2 mm lens vignetting + uneven bar lighting: 10-21 the corners get
// 37 % of the centre), so a part looks the same wherever it is cropped. Built by `des_tool calib-flat` from a blank
// white sheet that the belt carries across the view; saved as flat.json next to the config (des_app.json
// "flat_file"; calib-cam does not touch it). Part crops (roi_crop.hpp crop_part: NPU input, NG images, training
// set) are multiplied by ref / white. The geometric measurement and the locator work on the raw frame.
// Redo after moving the camera, the lights or changing the exposure.
#pragma once

#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace des {

struct FlatField {
    int gw = 0, gh = 0;                // grid cells; cell (i, j) centre at ((i + 0.5) * width / gw - 0.5, ...)
    int width = 1920, height = 1080;   // image the grid covers
    float ref = 0;                     // white level the gains normalise to (median in the centre of the view)
    std::vector<float> white;          // gw * gh white levels (gray 0..255), row-major
    float max_gain = 3.f;              // gains clamped to [1 / max_gain, max_gain]

    bool ok() const { return gw >= 2 && gh >= 2 && int(white.size()) == gw * gh && ref > 0; }
    float white_at(float x, float y) const;   // bilinear between cell centres, clamped at the border
    float gain_at(float x, float y) const;
};

// flat.json: {"grid_w", "grid_h", "width", "height", "ref", "white": [...]} (extra keys ignored)
std::string load_flat(const std::string &path, FlatField &f);
std::string save_flat(const std::string &path, const FlatField &f, const std::string &note);

// crop: BGR (8UC3); M: crop px (column, row, 1) -> image px, homogeneous 3x3 row-major (crop_part's mapping).
// The gain is evaluated on a grid of crop / 8 nodes and interpolated: the white level varies over tens of mm.
void apply_flat(cv::Mat &crop, const FlatField &f, const double M[9]);

struct FlatStats {
    int frames = 0;
    float covered = 0;        // fraction of the view seen under the sheet in at least one frame
    float centre = 0;         // = ref
    float min_rel = 0;        // darkest point / centre
    float corner_rel[4] = {}; // top-left, top-right, bottom-left, bottom-right / centre
    float resid_p5 = 0, resid_p95 = 0;   // sheet pixels after correction, relative to the centre
};

// Frames (CV_8UC1, full view) of the white sheet at several belt positions. A pixel counts as under the sheet in a
// frame when it is above 60 % of its own min..max range over all frames (so vignetting does not matter) and at
// least 25 px inside that frame's sheet region (the sheet edge, its shadow and the blur are left out).
class FlatBuilder {
public:
    void add(const cv::Mat &gray) { frames_.push_back(gray.clone()); }
    size_t size() const { return frames_.size(); }
    std::string build(FlatField &out, int gw = 48, int gh = 27, FlatStats *st = nullptr) const;

private:
    std::vector<cv::Mat> frames_;
};

}  // namespace des
