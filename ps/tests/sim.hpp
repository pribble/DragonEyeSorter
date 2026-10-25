// sim.hpp — shared simulation helpers of the scheduler tests (test_scheduler and test_belt_hil): a part riding the
// belt and the camera model that observes it. Header-only, no hardware, no OpenCV.
//
// Belt coordinates: a SimPart keeps its belt coordinate s (constant while it rides), so its table x is s + B for the
// belt travel B. Tracker / picker only ever see the simulated FrameObs, exactly like the real locate pipeline.
#pragma once

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "geom.hpp"
#include "tracker.hpp"

namespace des {

// One part riding the belt. cls = model class id (-1 = blob without NPU result). gone / picked are only used by the
// hardware-in-the-loop test (part left the belt / was taken by the simulated arm).
struct SimPart {
    float s, y, len, wid, axis;
    int cls;
    bool gone = false;
    bool picked = false;
};

// TrackerConfig of the tests: the 4-class synthetic model of 2026-10-20 (frozen fixture — models/label_list.json has
// since moved to 5 real classes; load_config builds tracker.rules from label_list the same way, the tests only need
// a fixed small table).
inline TrackerConfig tracker_cfg()
{
    TrackerConfig c;
    c.fov_x0 = 0; c.fov_x1 = 300; c.edge_margin = 10; c.decide_x = 270; c.tail_x = 600;
    c.rules = {{"gear_ok", "齿轮-良品", Disposition::Pick, 0},
               {"gear_short", "齿轮-缺料", Disposition::Flap, 1},
               {"gear_flash", "齿轮-飞边", Disposition::Flap, 2},
               {"gear_dirty", "齿轮-黑点/脏污", Disposition::Flap, 1}};
    return c;
}

// Camera model: parts fully inside the view (table x 0..300) are seen with noise, parts cut by an edge are reported
// as partial with a shifted center (the real locator's behavior).
inline FrameObs observe(const std::vector<SimPart> &parts, double B, uint32_t frame, std::mt19937 &rng)
{
    std::normal_distribution<float> n(0.f, 0.4f), na(0.f, 0.02f);
    FrameObs o;
    o.frame = frame;
    o.belt_mm = B;
    for (const SimPart &p : parts) {
        if (p.gone) continue;
        const float x = float(p.s + B), half = p.len / 2;
        if (x + half < 0 || x - half > 300) continue;
        Detection d;
        d.p = {x + n(rng), p.y + n(rng)};
        d.len = p.len + n(rng); d.wid = p.wid + n(rng); d.axis = wrap_axis(p.axis + na(rng));
        d.cls = p.cls; d.conf = p.cls >= 0 ? 0.9f : 0.f;
        d.partial = x - half < 0 || x + half > 300;
        if (d.partial) d.p.x += x - half < 0 ? 0.25f * half : -0.25f * half;   // cut blob: shifted center
        o.dets.push_back(d);
    }
    return o;
}

}  // namespace des
