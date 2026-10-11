// measure_config.hpp — settings of the geometric measurement (measure.hpp, track_measure.hpp). Plain structs, no
// OpenCV, so config.hpp can hold them (des_app.json "measure", docs/02 "几何测量").
#pragma once

#include "geom.hpp"

namespace des {

struct MeasureConfig {
    float camera_h_mm = 0;       // camera height above the belt; 0 = no parallax correction  ⚠ measure (H)
    Vec2 axis_table{150, 0};     // where the optical axis meets the belt, table mm (default: image center)
    float gear_h_mm = 10;        // part heights (top face above the belt)  ⚠ caliper, per recipe
    float rack_h_mm = 12;
    bool walls_visible = true;   // side walls bright in the image (silhouette = sweep) vs only the top face  ⚠ lighting
    float max_smear_mm = 0.8;    // above this parallax smear, tooth / gap defects are not judged
    float short_tooth_mm = 0.4;  // tooth tip this far below the tip circle = short (缺料)
    float flash_mm = 0.5;        // valley floor this far above the root circle = filled (飞边)
    // Rolling shutter: row r of the image is exposed later than row row0 by (r - row0) row times, during which the
    // belt moved belt_mm_per_row * (r - row0) along table +x. Edge points are moved back to the row0 instant.
    // Set per frame by the caller (track_measure); 0 = still belt / global shutter.
    float belt_mm_per_row = 0;
    float row0 = 0;              // in the coordinates of the image passed to measure_*()
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
    MeasureConfig m;
};

}  // namespace des
