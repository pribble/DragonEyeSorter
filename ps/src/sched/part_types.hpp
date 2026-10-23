// part_types.hpp — plain data types of the sorting library: detection, track state, class rule, measurement, recipe.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "geom.hpp"
namespace des {
struct Detection {
    Vec2 p;                 // table mm, center
    float len = 0, wid = 0; // mm, long / short side
    float axis = 0;         // rad, direction of the long side (wrap_axis)
    int cls = -1;           // model class id, -1 = blob only (no NPU result)
    float conf = 0;
    bool partial = false;   // touches the image border: presence only, geometry unreliable
    bool merged = false;    // blob larger than any single part: several touching parts
    float img_v = 0.5f;     // image row of the center, 0 = top .. 1 = bottom (rolling-shutter timing)
};
struct FrameObs {
    uint32_t frame = 0;     // PL frame count of the captured frame
    double belt_mm = 0;     // belt travel B at the capture instant
    std::vector<Detection> dets;
};

enum class TrackState : uint8_t { New, Tracking, Decided, Picking, Done };
enum class Disposition : uint8_t { None, Pick, Flap, Recheck };
// What a class means for sorting; built by the app from models/label_list.json (single source of the class table).
struct ClassRule {
    std::string label;      // model label ("gear_ok"), the CSV `label` column
    std::string cn;         // Chinese name for the HMI ("齿轮-良品"), filled by load_config
    Disposition disp = Disposition::Recheck;
    int dest = 0;           // Pick: bin index; Flap: outlet index
};
// Geometric measurement of one part (measure/, docs/视觉与检测.md "几何测量"): the frame nearest the optical axis wins
// (smallest parallax smear). Plain numbers, so this library stays free of OpenCV.
struct PartMeasure {
    bool valid = false;
    char kind = 0;              // 'g' gear, 'r' rack
    bool teeth_judged = false;  // false: parallax smear too large, the defect counts are not valid
    float axis_dist = 1e9f;     // mm from the optical axis when measured (selects the best frame)
    float smear = 0;            // mm, parallax smear of that frame
    uint32_t frame = 0;
    int count = 0;              // how many times this part was measured
    float size = 0;             // gear: OD; rack: length (mm)
    float bore = 0, ecc = 0;    // gear
    float pitch = 0;            // rack: mean pitch
    int teeth = 0;              // gear: spec count from the pitch; rack: tooth slots
    int short_teeth = 0, missing_teeth = 0, flash_gaps = 0;
    float hue = -1, sat = -1;   // part colour from its crop (deg, 0..255); -1 = no crop
    int color = -1;             // index into measure.colors, -1 = none matched / not classified
};
// Size / spec NG from the measurement (docs/视觉与检测.md 类别表: 尺寸类 NG 不进 NPU 类别, 由测量判 → 报废), applied only to
// parts the class vote would pick: it never upgrades an NPU NG.
struct SizeRule {
    bool enabled = false;           // ⚠ turn on once the camera height and calibration are real
    int gear_teeth = 30;            // spec (28-tooth parts = 规格混料)
    float gear_od = 32, gear_od_tol = 0.2f;    // 2026-10-24 user: ±0.2 (one gear flipped over reads 0.12 apart)
    float gear_bore = 6, gear_bore_tol = 0.3f;
    float gear_max_ecc = 0.3f;
    float rack_len = 50, rack_len_tol = 1.0f;
    int ng_outlet = 1;              // flap outlet for size / spec NG (index into outlets, 1 = scrap)
    bool defect_recheck = true;     // class says OK but the measurement found tooth defects -> recheck
    bool unjudged_recheck = true;   // class says OK but the teeth could not be judged (too far off the camera axis:
                                    // the side wall fills the far gaps, good gears read 3..14 missing teeth and the
                                    // OD is off too, 2026-10-24) -> recheck, before any size check
    bool require = false;           // no measurement by decide_x -> recheck
};
// Product recipe (配方): the part spec the size rule checks against; changeover = switch which spec is good, e.g.
// recipe "28-tooth gears": 30-tooth parts then fail the tooth count (规格混料) and go to scrap. Loaded from
// recipes.json; selected by the PLC (CAN RECIPE) or the operator (HMI), never while running.
struct Recipe {
    int id = 1;                     // 1..255 (CAN RECIPE carries one byte)
    std::string name;
    int gear_teeth = 30;
    float gear_od = 32, gear_od_tol = 0.2f;
    float gear_bore = 6, gear_bore_tol = 0.3f;
    float gear_max_ecc = 0.3f;
    float rack_len = 50, rack_len_tol = 1.0f;
    void apply(SizeRule &r) const   // spec fields only: enabled / outlet / recheck policy stay as configured
    {
        r.gear_teeth = gear_teeth; r.gear_od = gear_od; r.gear_od_tol = gear_od_tol; r.gear_bore = gear_bore;
        r.gear_bore_tol = gear_bore_tol; r.gear_max_ecc = gear_max_ecc; r.rack_len = rack_len; r.rack_len_tol = rack_len_tol;
    }
    static Recipe from_rule(const SizeRule &r, int id, const std::string &name)
    {
        Recipe c;
        c.id = id; c.name = name; c.gear_teeth = r.gear_teeth; c.gear_od = r.gear_od; c.gear_od_tol = r.gear_od_tol;
        c.gear_bore = r.gear_bore; c.gear_bore_tol = r.gear_bore_tol; c.gear_max_ecc = r.gear_max_ecc;
        c.rack_len = r.rack_len; c.rack_len_tol = r.rack_len_tol;
        return c;
    }
};
inline const Recipe *find_recipe(const std::vector<Recipe> &list, int id)
{
    for (const Recipe &r : list) if (r.id == id) return &r;
    return nullptr;
}
const char *to_str(TrackState s); const char *to_str(Disposition d);
}  // namespace des