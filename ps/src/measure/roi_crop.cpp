// roi_crop.cpp — see roi_crop.hpp
#include "roi_crop.hpp"

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

namespace des {

cv::Mat crop_part(const cv::Mat &src, const Homography &px_to_table, Vec2 center, float axis, const CropSpec &spec)
{
    const int N = spec.out_px;
    const double s = double(spec.side_mm) / N;
    // crop x along the part axis; crop y (down) = axis rotated -90 deg in the right-handed table frame, which keeps
    // the crop a rotation (not a mirror) of the camera image
    const double ux = std::cos(axis), uy = std::sin(axis), dx = std::sin(axis), dy = -std::cos(axis);
    const double h0 = 0.5 * N - 0.5;
    // A: crop px (i, j, 1) -> table mm
    const double A[9] = {ux * s, dx * s, center.x - (ux + dx) * s * h0,
                         uy * s, dy * s, center.y - (uy + dy) * s * h0,
                         0, 0, 1};
    const Homography hi = px_to_table.inverse();
    double M[9];   // crop px -> image px = Hinv * A
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) M[3 * r + c] = hi.h[3 * r] * A[c] + hi.h[3 * r + 1] * A[3 + c] + hi.h[3 * r + 2] * A[6 + c];

    // bounding box of the crop in the image (+2 px for interpolation), copied once
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (double ci : {-0.5, N - 0.5})
        for (double cj : {-0.5, N - 0.5}) {
            double w = M[6] * ci + M[7] * cj + M[8];
            double x = (M[0] * ci + M[1] * cj + M[2]) / w, y = (M[3] * ci + M[4] * cj + M[5]) / w;
            x0 = std::min(x0, x); x1 = std::max(x1, x);
            y0 = std::min(y0, y); y1 = std::max(y1, y);
        }
    cv::Rect box(int(std::floor(x0)) - 2, int(std::floor(y0)) - 2, 0, 0);
    box.width = int(std::ceil(x1)) + 3 - box.x;
    box.height = int(std::ceil(y1)) + 3 - box.y;
    const cv::Rect in = box & cv::Rect(0, 0, src.cols, src.rows);
    cv::Mat local(box.size(), src.type(), cv::Scalar::all(0));   // outside the image = black
    if (in.area() > 0) src(in).copyTo(local(in - box.tl()));

    // shift M into the local copy: x_local = x - box.x
    cv::Matx33d Mm(M[0] - box.x * M[6], M[1] - box.x * M[7], M[2] - box.x * M[8],
                   M[3] - box.y * M[6], M[4] - box.y * M[7], M[5] - box.y * M[8],
                   M[6], M[7], M[8]);
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
    cv::Mat hsv, ch[3];
    cv::cvtColor(crop, hsv, cv::COLOR_BGR2HSV);
    cv::split(hsv, ch);
    const int n = crop.cols, r = int(0.42 * n), c0 = n / 2;
    cv::Mat disc = cv::Mat::zeros(crop.size(), CV_8U);
    cv::circle(disc, cv::Point(c0, c0), r, cv::Scalar(255), cv::FILLED);
    cv::Mat v_in;
    ch[2].copyTo(v_in, disc);
    cv::Mat bright;
    cv::threshold(v_in, bright, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    bright &= disc;
    double sx = 0, sy = 0, ss = 0, sv = 0;
    int k = 0;
    for (int y = 0; y < n; y++) {
        const uint8_t *m = bright.ptr<uint8_t>(y), *h = ch[0].ptr<uint8_t>(y), *sa = ch[1].ptr<uint8_t>(y),
                      *va = ch[2].ptr<uint8_t>(y);
        for (int x = 0; x < n; x++) {
            if (!m[x]) continue;
            const double a = h[x] * (2.0 * CV_PI / 180.0);   // OpenCV 8-bit hue is 0..179 (2 deg per step)
            sx += sa[x] * std::cos(a);
            sy += sa[x] * std::sin(a);
            ss += sa[x];
            sv += va[x];
            k++;
        }
    }
    if (k == 0) return out;
    out.pixels = k;
    out.sat = float(ss / k);
    out.val = float(sv / k);
    double deg = std::atan2(sy, sx) * 180.0 / CV_PI;
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
