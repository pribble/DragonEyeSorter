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
    return out;
}

}  // namespace des
