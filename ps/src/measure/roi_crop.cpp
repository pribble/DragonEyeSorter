// roi_crop.cpp — see roi_crop.hpp
#include "roi_crop.hpp"
#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include "fit.hpp"
namespace des {

cv::Mat crop_part(const cv::Mat &src, const Homography &px_to_table, Vec2 center, float axis, const CropSpec &spec)
{
    const int N = spec.out_px;
    const double s = double(spec.side_mm) / N;
    // crop x along the part axis; crop y (down) = axis rotated -90 deg (right-handed table frame), so the crop stays
    const double ux = std::cos(axis), uy = std::sin(axis), dx = std::sin(axis), dy = -std::cos(axis);
    const double h0 = 0.5 * N - 0.5;
    const double A[9] = {ux * s, dx * s, center.x - (ux + dx) * s * h0,   // A: crop px (i, j, 1) -> table mm
                         uy * s, dy * s, center.y - (uy + dy) * s * h0, 0, 0, 1};
    const Homography hi = px_to_table.inverse();
    double M[9];   // crop px -> image px = Hinv * A
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            M[3 * r + c] = hi.h[3 * r] * A[c] + hi.h[3 * r + 1] * A[3 + c] + hi.h[3 * r + 2] * A[6 + c];
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;   // bounding box of the crop in the image (+2 px)
    for (double ci : {-0.5, N - 0.5})
        for (double cj : {-0.5, N - 0.5}) {
            const double w = M[6] * ci + M[7] * cj + M[8];
            const double x = (M[0] * ci + M[1] * cj + M[2]) / w, y = (M[3] * ci + M[4] * cj + M[5]) / w;
            x0 = std::min(x0, x); x1 = std::max(x1, x);
            y0 = std::min(y0, y); y1 = std::max(y1, y);
        }
    cv::Rect box(int(std::floor(x0)) - 2, int(std::floor(y0)) - 2, 0, 0);
    box.width = int(std::ceil(x1)) + 3 - box.x; box.height = int(std::ceil(y1)) + 3 - box.y;
    const cv::Rect in = box & cv::Rect(0, 0, src.cols, src.rows);
    static thread_local cv::Mat local;   // per-thread scratch: same pixels, no per-crop allocation
    if (local.size() != box.size() || local.type() != src.type()) local.create(box.size(), src.type());
    local.setTo(cv::Scalar::all(0));   // outside the image = black
    if (in.area() > 0) src(in).copyTo(local(in - box.tl()));
    cv::Matx33d Mm(M[0] - box.x * M[6], M[1] - box.x * M[7], M[2] - box.x * M[8],   // M shifted into the local copy
                   M[3] - box.y * M[6], M[4] - box.y * M[7], M[5] - box.y * M[8], M[6], M[7], M[8]);
    cv::Mat out;
    cv::warpPerspective(local, out, cv::Mat(Mm), cv::Size(N, N), cv::INTER_LINEAR | cv::WARP_INVERSE_MAP,
                        cv::BORDER_CONSTANT, cv::Scalar::all(0));
    if (out.channels() == 4) cv::cvtColor(out, out, cv::COLOR_BGRA2BGR);
    else if (out.channels() == 1) cv::cvtColor(out, out, cv::COLOR_GRAY2BGR);
    if (spec.flat && spec.flat->ok()) apply_flat(out, *spec.flat, M);
    return out;
}

PartColor part_color(const cv::Mat &crop)
{
    PartColor out;
    if (crop.empty() || crop.channels() != 3) return out;
    const int n = crop.cols, r = int(0.42 * n), c0 = n / 2;
    static thread_local cv::Mat disc;   // centre disc: depends only on the crop size -> built once per thread
    static thread_local int disc_n = 0;
    if (disc_n != n || disc.size() != crop.size()) {
        disc = cv::Mat::zeros(crop.size(), CV_8U);
        cv::circle(disc, cv::Point(c0, c0), r, cv::Scalar(255), cv::FILLED);
        disc_n = n;
    }
    static thread_local cv::Mat hsv;
    cv::cvtColor(crop, hsv, cv::COLOR_BGR2HSV);
    // Otsu of V over the disc with V = 0 outside it (what the masked copy used to feed cv::threshold): own histogram +
    // otsu8, so no masked copy, no bright image and no split; the pixels go straight from the HSV image
    int hist[256] = {0};
    for (int y = 0; y < crop.rows; y++) {
        const uint8_t *m = disc.ptr<uint8_t>(y), *p = hsv.ptr<uint8_t>(y);
        for (int x = 0; x < n; x++) hist[m[x] ? p[3 * x + 2] : 0]++;
    }
    const int t = meas::otsu8(hist, size_t(crop.rows) * size_t(n));
    double sx = 0, sy = 0, ss = 0, sv = 0;
    int k = 0;
    for (int y = 0; y < crop.rows; y++) {
        const uint8_t *m = disc.ptr<uint8_t>(y), *p = hsv.ptr<uint8_t>(y);
        for (int x = 0; x < n; x++) {
            if (!m[x]) continue;
            const uint8_t v = p[3 * x + 2];
            if (v <= t) continue;   // bright part pixels only (V > Otsu, inside the disc)
            const double a = p[3 * x] * (2.0 * CV_PI / 180.0);   // 8-bit hue is 0..179 (2 deg per step)
            sx += p[3 * x + 1] * std::cos(a);
            sy += p[3 * x + 1] * std::sin(a);
            ss += p[3 * x + 1];
            sv += v;
            k++;
        }
    }
    if (k == 0) return out;
    out.pixels = k; out.sat = float(ss / k); out.val = float(sv / k);
    const double deg = std::atan2(sy, sx) * 180.0 / CV_PI;
    out.hue = float(deg < 0 ? deg + 360 : deg);
    return out;
}

int classify_color(const std::vector<ColorClass> &classes, const PartColor &c)
{
    if (c.pixels == 0) return -1;
    for (size_t i = 0; i < classes.size(); i++) {
        const ColorClass &k = classes[i];
        if (c.sat < k.sat_lo || c.sat > k.sat_hi) continue;
        const bool hue_ok = k.hue_lo <= k.hue_hi ? (c.hue >= k.hue_lo && c.hue <= k.hue_hi)
                                                 : (c.hue >= k.hue_lo || c.hue <= k.hue_hi);   // wraps through 0
        if (hue_ok) return int(i);
    }
    return -1;
}
}  // namespace des
