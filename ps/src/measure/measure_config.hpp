// measure_config.hpp — settings of the geometric measurement (measure.hpp, track_measure.hpp). Plain structs, no
// OpenCV, so config.hpp can hold them (des_app.json "measure", docs/视觉与检测.md "几何测量").
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "geom.hpp"

namespace des {

struct FlatField;   // flat_field.hpp

struct MeasureConfig {
    float camera_h_mm = 0;       // camera height above the belt; 0 = no parallax correction  ⚠ measure (H)
    Vec2 axis_table{150, 0};     // where the optical axis meets the belt, table mm (default: image center)
    float gear_h_mm = 10;        // part heights (top face above the belt)  ⚠ caliper, per recipe
    float rack_h_mm = 12;
    bool walls_visible = true;   // side walls bright in the image (silhouette = sweep) vs only the top face  ⚠ lighting
    float max_smear_mm = 0.8;    // above this parallax smear, tooth / gap defects are not judged
    float short_tooth_mm = 0.4;  // tooth tip this far below the tip circle = short (缺料)
    float flash_mm = 0.5;        // valley floor this far above the root circle = filled (飞边)
    // Master-part offsets, added to the reported OD / bore only (tooth judgments use the raw fit): a narrow, blurred
    // tooth tip never reaches the full part gray level, so its 50 % edge sits inside the true tip and the tip circle
    // reads small (the root circle and the bore do not). Set from a caliper-measured gear at the view centre
    // (des_tool measure -n 20); redo after changing the exposure, the light or the camera.
    float od_offset_mm = 0;
    float bore_offset_mm = 0;
    // bore off-centre from the clean near-axis rim arc only (measure.cpp edges()); off until checked on real gears
    // (10-21 sweep: same gear 0.14 mm spread instead of 0.67, but wrong > 160 mm off and the offset must be redone)
    bool bore_near_arc = false;
    // Rolling shutter: row r of the image is exposed later than row row0 by (r - row0) row times, during which the
    // belt moved belt_mm_per_row * (r - row0) along table +x. Edge points are moved back to the row0 instant.
    // Set per frame by the caller (track_measure); 0 = still belt / global shutter.
    float belt_mm_per_row = 0;
    float row0 = 0;              // in the coordinates of the image passed to measure_*()
};

// Parallax of the part position. The belt-plane calibration (px_to_table) puts a point h above the belt outward from
// O (where the optical axis meets the belt) by k = H / (H - h). Locating, tracking and picking want where the part
// stands on the belt, so they use px_to_part = (scale by 1/k about O) o px_to_table - still a homography, so every
// image <-> table mapping of part positions (overlay, ROIs, crops) stays a plain map / inverse. The blob is the top
// face (walls dark: k = H / (H - h)) or the top face swept down to the belt by bright walls (blob centre halfway
// between the two: k = (2H - h) / (2(H - h))). The geometric measurement keeps px_to_table: it corrects each edge
// point itself. camera_h_mm = 0 (not measured yet): no correction.
inline Homography part_homography(const Homography &px_to_table, const MeasureConfig &m, Vec2 axis)
{
    const double H = m.camera_h_mm, h = m.gear_h_mm;
    if (H <= 0 || h <= 0 || h >= H) return px_to_table;
    const double f = m.walls_visible ? 2 * (H - h) / (2 * H - h) : (H - h) / H;   // 1 / k
    const double S[9] = {f, 0, axis.x * (1 - f), 0, f, axis.y * (1 - f), 0, 0, 1};
    Homography r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r.h[3 * i + j] = S[3 * i] * px_to_table.h[j] + S[3 * i + 1] * px_to_table.h[3 + j] +
                             S[3 * i + 2] * px_to_table.h[6 + j];
    return r;
}

// Part colour classes (roi_crop part_color / classify_color): first match wins; hue in degrees (hue_lo > hue_hi wraps
// through 0, e.g. red 340..10), sat = mean saturation of the part pixels on OpenCV's 0..255 scale. White / natural
// parts have a low saturation whatever their hue. ⚠ thresholds from real parts under the final light
struct ColorClass {
    std::string name;
    float hue_lo = 0, hue_hi = 360;
    float sat_lo = 0, sat_hi = 255;
};

// Measuring tracked parts from the live worker (track_measure.hpp)
struct TrackMeasureConfig {
    bool enabled = true;
    int per_frame = 1;           // parts measured per worker frame (4~5 ms each on the A35)
    float gear_ratio = 0.6f;     // fused blob wid / len above this = gear, else rack
    float margin_mm = 3;         // ROI margin around the part footprint
    float improve = 0.75f;       // re-measure when the axis distance drops below improve * stored - 1 mm
    bool axis_from_image = true; // optical axis = image center mapped to the table (else m.axis_table as given)
    float crop_side_mm = 56;     // part crop taken with every successful measurement (roi_crop.hpp: NPU input,
    int crop_px = 224;           // NG images, training set); 0 = no crop
    std::vector<ColorClass> colors;   // colour from every crop; empty = not classified (hue / sat still logged)
    std::shared_ptr<const FlatField> flat;   // flat.json (des_tool calib-flat) applied to the crops; null = raw
    MeasureConfig m;
};

}  // namespace des
